/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * uvcprobe: a UVC camera through usb_raw, without the media kit (docs/milestones.md
 * M45, docs/m45-uvc-facts.md §B).
 *
 *   uvcprobe <device> modes [<format>]
 *       for each frame size of the format (1: the first, uncompressed on
 *       the C920; 2: MJPEG), at its fastest interval: SET_CUR(PROBE), then
 *       GET_CUR(PROBE); prints the frame and payload sizes the camera asks
 *       for and the alternate setting that carries them.
 *   uvcprobe <device> stream <format> <frame> <fps> <seconds>
 *           [<packets>] [<file>] [<queued>]
 *       probe, commit, the alternate setting, then isochronous transfers of
 *       <packets> microframes (default 64) for <seconds>, <queued> of them
 *       in flight together (default 4; 0: one at a time, the old blocking
 *       call); prints what the
 *       payload headers say (header lengths, FID toggles, EOF, errors, PTS
 *       and SCR), frames and their sizes, packets that came back empty or
 *       in error, and how long transfers took; with <file>, the last whole
 *       frame is written to it (the first ones are dark until the camera's
 *       exposure has settled).
 *
 * <device>: /dev/bus/usb/... (listusb shows it). Build: $CXX -O2
 * $HAIKU_CPPFLAGS -o uvcprobe uvcprobe.cpp -lbe -ldevice
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>
#include <USBKit.h>


static const uint8 kSetCur = 0x01;
static const uint8 kGetCur = 0x81;
static const uint8 kProbe = 0x01;
static const uint8 kCommit = 0x02;
static const size_t kProbeLength = 26;
	// UVC 1.0 (the C920's bcdUVC); 34 from UVC 1.1 on


struct Frame {
	uint8	format;
	uint8	index;
	uint16	width;
	uint16	height;
	uint32	interval;		// the fastest, 100 ns units
	uint32	bufferSize;		// uncompressed: the frame's bytes
};

static Frame sFrames[64];
static int sFrameCount;
static int sFormatCount;
static uint8 sStreamingInterface = 0xff;


static uint32
read32(const uint8* data)
{
	return data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32)data[3] << 24);
}


static uint16
read16(const uint8* data)
{
	return data[0] | (data[1] << 8);
}


/*!	The video streaming interface's format and frame descriptors. */
static void
parse_descriptors(BUSBDevice& device)
{
	const BUSBConfiguration* configuration = device.ActiveConfiguration();
	if (configuration == NULL)
		return;
	int format = 0;
	for (uint32 i = 0; i < configuration->CountInterfaces(); i++) {
		const BUSBInterface* interface = configuration->InterfaceAt(i);
		if (interface->Class() != 0x0e || interface->Subclass() != 0x02)
			continue;
		sStreamingInterface = interface->Index();
		usb_descriptor* descriptor = (usb_descriptor*)malloc(1024);
		for (uint32 d = 0; interface->OtherDescriptorAt(d, descriptor, 1024)
				== B_OK; d++) {
			const uint8* raw = (const uint8*)descriptor;
			if (raw[1] != 0x24)
				continue;
			uint8 subtype = raw[2];
			if (subtype == 0x04 || subtype == 0x06) {
				// uncompressed or MJPEG format
				format = raw[3];
				sFormatCount++;
			} else if ((subtype == 0x05 || subtype == 0x07)
				&& sFrameCount < 64) {
				Frame& frame = sFrames[sFrameCount++];
				frame.format = format;
				frame.index = raw[3];
				frame.width = read16(raw + 5);
				frame.height = read16(raw + 7);
				frame.bufferSize = read32(raw + 17);
				frame.interval = read32(raw + 21);
					// the default; the list follows
				uint8 types = raw[25];
				if (types == 0)
					frame.interval = read32(raw + 26);
						// continuous: the minimum
				else {
					for (uint8 t = 0; t < types; t++) {
						uint32 interval = read32(raw + 26 + t * 4);
						if (interval < frame.interval)
							frame.interval = interval;
					}
				}
			}
		}
		free(descriptor);
		break;
	}
}


static const Frame*
find_frame(int format, int index)
{
	for (int i = 0; i < sFrameCount; i++) {
		if (sFrames[i].format == format && sFrames[i].index == index)
			return &sFrames[i];
	}
	return NULL;
}


static ssize_t
control(BUSBDevice& device, uint8 request, uint8 selector, uint8* data,
	size_t length)
{
	uint8 type = (request & 0x80) != 0 ? 0xa1 : 0x21;
	return device.ControlTransfer(type, request, selector << 8,
		sStreamingInterface, length, data);
}


/*!	SET_CUR(PROBE), then GET_CUR(PROBE) into \a probe. */
static status_t
probe(BUSBDevice& device, int format, int frame, uint32 interval,
	uint8* probe)
{
	memset(probe, 0, kProbeLength);
	probe[0] = 1;
		// bmHint: the frame interval stays as asked
	probe[2] = format;
	probe[3] = frame;
	probe[4] = interval & 0xff;
	probe[5] = (interval >> 8) & 0xff;
	probe[6] = (interval >> 16) & 0xff;
	probe[7] = interval >> 24;
	ssize_t result = control(device, kSetCur, kProbe, probe, kProbeLength);
	if (result < 0)
		return result;
	result = control(device, kGetCur, kProbe, probe, kProbeLength);
	if (result < 0)
		return result;
	if (result != (ssize_t)kProbeLength)
		return B_BAD_DATA;
	return B_OK;
}


static uint32 sAlternatePayload[32];
	// bytes per microframe of each alternate setting of the streaming
	// interface: its endpoint's packet size times its transactions
static int sAlternateCount;


/*!	The streaming interface's alternate settings, from the configuration
	descriptor (the endpoints of the ones not active aren't otherwise
	known without SET_INTERFACE).
*/
static void
parse_alternates(BUSBDevice& device)
{
	// GET_DESCRIPTOR itself: BUSBDevice::GetDescriptor() comes back empty
	// for the configuration
	uint8 header[9];
	if (device.ControlTransfer(0x80, 0x06, USB_DESCRIPTOR_CONFIGURATION << 8,
			0, sizeof(header), header) < (ssize_t)sizeof(header)) {
		return;
	}
	uint16 total = read16(header + 2);
	uint8* raw = (uint8*)malloc(total);
	if (raw == NULL || device.ControlTransfer(0x80, 0x06,
			USB_DESCRIPTOR_CONFIGURATION << 8, 0, total, raw)
				< (ssize_t)total) {
		free(raw);
		return;
	}
	int interface = -1;
	int alternate = 0;
	for (uint32 offset = 0; offset + 2 <= total && raw[offset] >= 2;
			offset += raw[offset]) {
		const uint8* d = raw + offset;
		if (d[1] == USB_DESCRIPTOR_INTERFACE) {
			interface = d[2];
			alternate = d[3];
		} else if (d[1] == USB_DESCRIPTOR_ENDPOINT
			&& interface == sStreamingInterface && alternate < 32
			&& (d[3] & 3) == 1) {
			uint16 field = read16(d + 4);
			sAlternatePayload[alternate]
				= (field & 0x7ff) * (1 + ((field >> 11) & 3));
			if (alternate + 1 > sAlternateCount)
				sAlternateCount = alternate + 1;
		}
	}
	free(raw);
}


/*!	The smallest alternate setting whose payload per microframe holds
	\a payload bytes; its payload in \a _alternatePayload.
*/
static int
choose_alternate(uint32 payload, uint32* _alternatePayload)
{
	int best = -1;
	for (int alternate = 1; alternate < sAlternateCount; alternate++) {
		uint32 bytes = sAlternatePayload[alternate];
		if (bytes >= payload
			&& (best < 0 || bytes < sAlternatePayload[best])) {
			best = alternate;
		}
	}
	*_alternatePayload = best >= 0 ? sAlternatePayload[best] : 0;
	return best;
}


static int
modes(BUSBDevice& device, int onlyFormat)
{
	printf("%d formats, %d frames; streaming interface %u, alternates:",
		sFormatCount, sFrameCount, sStreamingInterface);
	for (int i = 1; i < sAlternateCount; i++)
		printf(" %d:%u", i, sAlternatePayload[i]);
	printf("\n");
	for (int i = 0; i < sFrameCount; i++) {
		const Frame& frame = sFrames[i];
		if (onlyFormat != 0 && frame.format != onlyFormat)
			continue;
		uint8 result[kProbeLength];
		status_t status = probe(device, frame.format, frame.index,
			frame.interval, result);
		if (status != B_OK) {
			printf("format %u frame %2u %4ux%-4u: probe failed: %s\n",
				frame.format, frame.index, frame.width, frame.height,
				strerror(status));
			continue;
		}
		uint32 frameSize = read32(result + 18);
		uint32 payload = read32(result + 22);
		uint32 interval = read32(result + 4);
		uint32 alternatePayload;
		int alternate = choose_alternate(payload, &alternatePayload);
		printf("format %u frame %2u %4ux%-4u %5.1f fps: frame %7u bytes, "
			"payload %4u bytes a microframe -> alt %d (%u)\n", result[2],
			result[3], frame.width, frame.height,
			interval != 0 ? 1e7 / interval : 0.0, frameSize, payload,
			alternate, alternatePayload);
	}
	return 0;
}


static int
stream(BUSBDevice& device, int format, int frameIndex, double fps,
	double seconds, int packetCount, const char* path, int queued)
{
	const Frame* frame = find_frame(format, frameIndex);
	if (frame == NULL) {
		fprintf(stderr, "uvcprobe: no frame %d of format %d\n", frameIndex,
			format);
		return 1;
	}
	uint32 interval = fps > 0 ? (uint32)(1e7 / fps + 0.5) : frame->interval;
	uint8 negotiated[kProbeLength];
	status_t status = probe(device, format, frameIndex, interval, negotiated);
	if (status != B_OK) {
		fprintf(stderr, "uvcprobe: probe: %s\n", strerror(status));
		return 1;
	}
	if (control(device, kSetCur, kCommit, negotiated, kProbeLength) < 0) {
		fprintf(stderr, "uvcprobe: commit failed\n");
		return 1;
	}
	uint32 payload = read32(negotiated + 22);
	uint32 frameSize = read32(negotiated + 18);
	uint32 alternatePayload;
	int alternate = choose_alternate(payload, &alternatePayload);
	if (alternate < 0) {
		fprintf(stderr, "uvcprobe: no alternate setting for %u bytes\n",
			payload);
		return 1;
	}
	const BUSBConfiguration* configuration = device.ActiveConfiguration();
	BUSBInterface* interface
		= (BUSBInterface*)configuration->InterfaceAt(sStreamingInterface);
	if (interface->SetAlternate(alternate) != B_OK
		|| interface->CountEndpoints() == 0) {
		fprintf(stderr, "uvcprobe: alternate %d failed\n", alternate);
		return 1;
	}
	const BUSBEndpoint* endpoint = interface->EndpointAt(0);
	printf("%ux%u at %.1f fps: frame %u bytes, payload %u a microframe; alt %d "
		"(%u), endpoint %#x, %d microframes a transfer\n", frame->width,
		frame->height, 1e7 / read32(negotiated + 4), frameSize, payload,
		alternate, alternatePayload, endpoint->Descriptor()->endpoint_address, packetCount);

	size_t length = (size_t)alternatePayload * packetCount;
	uint8* data = (uint8*)malloc(length);
	usb_iso_packet_descriptor* packets = (usb_iso_packet_descriptor*)calloc(
		packetCount, sizeof(usb_iso_packet_descriptor));
	uint8* frameData = (uint8*)malloc(frameSize + 65536);
	uint8* lastFrame = (uint8*)malloc(frameSize + 65536);
	uint32 lastFrameBytes = 0;
	if (data == NULL || packets == NULL || frameData == NULL
		|| lastFrame == NULL) {
		return 1;
	}

	// statistics
	int64 transfers = 0, failed = 0, packetsTotal = 0, packetsEmpty = 0;
	int64 packetsError = 0, headerOnly = 0, badHeaders = 0, errBits = 0;
	int64 bytes = 0, frames = 0, eofs = 0, shortFrames = 0;
	int headerLengths[16] = {};
	bigtime_t longest = 0;
	uint32 frameBytes = 0;
	int lastFid = -1;
	bool written = path == NULL;
	bool started = false;
	uint32 smallest = 0xffffffff, largest = 0;
	bool dht = false;

	// queued transfers: buffers, descriptors and request numbers
	int slots = queued > 0 ? queued : 1;
	uint8* buffers[16];
	usb_iso_packet_descriptor* descriptors[16];
	uint32 requests[16];
	for (int q = 0; q < slots; q++) {
		buffers[q] = q == 0 ? data : (uint8*)malloc(length);
		descriptors[q] = q == 0 ? packets
			: (usb_iso_packet_descriptor*)calloc(packetCount,
				sizeof(usb_iso_packet_descriptor));
		if (buffers[q] == NULL || descriptors[q] == NULL)
			return 1;
	}
	BUSBEndpoint* isoEndpoint = (BUSBEndpoint*)endpoint;
	int inFlight = 0;

	bigtime_t start = system_time();
	bigtime_t end = start + (bigtime_t)(seconds * 1e6);
	int next = 0;
	while (true) {
		// keep the queue full while there is time
		while (queued > 0 && inFlight < slots && system_time() < end) {
			int q = (next + inFlight) % slots;
			for (int i = 0; i < packetCount; i++) {
				descriptors[q][i].request_length = alternatePayload;
				descriptors[q][i].actual_length = 0;
				descriptors[q][i].status = B_OK;
			}
			if (isoEndpoint->QueueIsochronous(buffers[q], length,
					descriptors[q], packetCount, &requests[q]) != B_OK) {
				failed++;
				if (failed <= 3)
					fprintf(stderr, "uvcprobe: queueing failed\n");
				break;
			}
			inFlight++;
		}
		if (queued > 0 && inFlight == 0)
			break;
		if (queued == 0 && system_time() >= end)
			break;

		int q = next;
		bigtime_t before = system_time();
		ssize_t result;
		if (queued > 0) {
			result = isoEndpoint->WaitIsochronous(requests[q]);
			inFlight--;
			next = (next + 1) % slots;
		} else {
			for (int i = 0; i < packetCount; i++) {
				packets[i].request_length = alternatePayload;
				packets[i].actual_length = 0;
				packets[i].status = B_OK;
			}
			result = isoEndpoint->IsochronousTransfer(data, length, packets,
				packetCount);
		}
		bigtime_t took = system_time() - before;
		if (took > longest)
			longest = took;
		transfers++;
		if (result < 0) {
			failed++;
			if (failed <= 3)
				fprintf(stderr, "uvcprobe: transfer: %s\n", strerror(result));
			if (failed > 20)
				break;
			continue;
		}

		const uint8* data = buffers[q];
		const usb_iso_packet_descriptor* packets = descriptors[q];
		for (int i = 0; i < packetCount; i++) {
			// packet i sits after the earlier ones' request lengths
			const uint8* packet = data + (size_t)i * alternatePayload;
			int actual = packets[i].actual_length;
			packetsTotal++;
			if (packets[i].status != B_OK) {
				packetsError++;
				continue;
			}
			if (actual == 0) {
				packetsEmpty++;
				continue;
			}
			uint8 headerLength = packet[0];
			if (headerLength < 2 || headerLength > actual) {
				badHeaders++;
				continue;
			}
			headerLengths[headerLength & 15]++;
			uint8 info = packet[1];
			int fid = info & 1;
			if ((info & 0x40) != 0)
				errBits++;
			if (fid != lastFid) {
				// a new frame begins: the one before ends
				if (lastFid >= 0 && started && frameBytes > 0) {
					frames++;
					if (frameBytes < smallest)
						smallest = frameBytes;
					if (frameBytes > largest)
						largest = frameBytes;
					if (format == 1 && frameBytes != frameSize)
						shortFrames++;
					if ((format != 1 || frameBytes == frameSize)
						&& frameBytes <= frameSize + 65536) {
						memcpy(lastFrame, frameData, frameBytes);
						lastFrameBytes = frameBytes;
					}
				}
				started = lastFid >= 0;
				lastFid = fid;
				frameBytes = 0;
			}
			int dataLength = actual - headerLength;
			if (dataLength > 0) {
				if (frameBytes + dataLength <= frameSize + 65536) {
					memcpy(frameData + frameBytes, packet + headerLength,
						dataLength);
				}
				if (format != 1 && frameBytes < 1024) {
					for (int b = 0; b + 1 < dataLength
							&& frameBytes + b < 1024; b++) {
						if (packet[headerLength + b] == 0xff
							&& packet[headerLength + b + 1] == 0xc4)
							dht = true;
					}
				}
				frameBytes += dataLength;
				bytes += dataLength;
			} else
				headerOnly++;
			if ((info & 2) != 0)
				eofs++;
		}
	}
	bigtime_t elapsed = system_time() - start;
	interface->SetAlternate(0);
	if (!written && lastFrameBytes > 0) {
		FILE* file = fopen(path, "wb");
		if (file != NULL) {
			fwrite(lastFrame, 1, lastFrameBytes, file);
			fclose(file);
			written = true;
		}
	}

	printf("%.1f s: %" B_PRId64 " transfers (%" B_PRId64 " failed, %d "
		"queued together), longest wait %.1f ms for %d microframes (%.1f "
		"ms)\n", elapsed / 1e6, transfers, failed, queued, longest / 1e3,
		packetCount, packetCount * 0.125);
	printf("packets: %" B_PRId64 ", %" B_PRId64 " empty, %" B_PRId64
		" in error, %" B_PRId64 " header only, %" B_PRId64 " bad headers, %"
		B_PRId64 " with ERR\n", packetsTotal, packetsEmpty, packetsError,
		headerOnly, badHeaders, errBits);
	printf("header lengths:");
	for (int i = 0; i < 16; i++) {
		if (headerLengths[i] != 0)
			printf(" %d bytes x%d", i, headerLengths[i]);
	}
	printf("\n");
	printf("data: %.2f MB/s; %" B_PRId64 " frames (%.1f a second), %" B_PRId64
		" EOF marks", bytes / (elapsed / 1e6) / 1e6, frames,
		frames / (elapsed / 1e6), eofs);
	if (frames > 0)
		printf(", sizes %u to %u", smallest, largest);
	if (format == 1)
		printf(", %" B_PRId64 " not %u bytes", shortFrames, frameSize);
	else
		printf(", DHT %s", dht ? "seen" : "not seen in the first 1 KiB");
	printf("\n");
	if (path != NULL)
		printf("%s: %s\n", path, written ? "a frame" : "no whole frame");
	for (int q = 1; q < slots; q++) {
		free(buffers[q]);
		free(descriptors[q]);
	}
	free(data);
	free(packets);
	free(frameData);
	free(lastFrame);
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: uvcprobe <device> modes [<format>]\n"
			"       uvcprobe <device> stream <format> <frame> <fps> "
			"<seconds> [<packets>] [<file>|-] [<queued>]\n");
		return 1;
	}
	BUSBDevice device(argv[1]);
	if (device.InitCheck() != B_OK) {
		fprintf(stderr, "uvcprobe: %s: %s\n", argv[1],
			strerror(device.InitCheck()));
		return 1;
	}
	parse_descriptors(device);
	if (sStreamingInterface == 0xff) {
		fprintf(stderr, "uvcprobe: no video streaming interface\n");
		return 1;
	}
	parse_alternates(device);
	if (strcmp(argv[2], "modes") == 0)
		return modes(device, argc > 3 ? atoi(argv[3]) : 0);
	if (strcmp(argv[2], "stream") == 0 && argc >= 7) {
		return stream(device, atoi(argv[3]), atoi(argv[4]), atof(argv[5]),
			atof(argv[6]), argc > 7 ? atoi(argv[7]) : 64,
			argc > 8 && strcmp(argv[8], "-") != 0 ? argv[8] : NULL,
			argc > 9 ? min_c(atoi(argv[9]), 16) : 4);
	}
	fprintf(stderr, "uvcprobe: unknown command\n");
	return 1;
}
