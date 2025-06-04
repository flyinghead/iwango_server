#include "common.h"
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <stdlib.h>
#include <string>

using sstream = std::stringstream;

uint32_t rotateRight(uint32_t v, int r) {
	return (v >> r) | (v << (32 - r));
}
uint32_t rot28xorEvenBits(uint32_t v) {
	return rotateRight(v, 28) ^ 0xaaaaaaaa;
}

// ccitt-crc-false with an extra inversion
uint16_t crc16(uint8_t *pData, unsigned length)
{
	uint16_t wCrc = 0xffff;
	while (length--)
	{
		wCrc ^= *pData++ << 8;
		for (int i = 0; i < 8; i++)
			wCrc = wCrc & 0x8000 ? (wCrc << 1) ^ 0x1021 : wCrc << 1;
	}
	wCrc = ~wCrc;
	fprintf(stderr, "crc %04x\n", wCrc);
	return wCrc;
}

int main(int argc, char *argv[])
{
	uint8_t data[] {
		0,		// cipher (none)
		64, 0,	// size
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	};

	uint32_t v = (sizeof(data) << 16) | crc16(data, sizeof(data));
	fprintf(stderr, "rot28xor: %08x -> ", v);
	v = rot28xorEvenBits(v);
	fprintf(stderr, "%08x\n", v);
	fwrite(&v, 4, 1, stdout);
	fwrite(data, sizeof(data), 1, stdout);

	return 0;
}
