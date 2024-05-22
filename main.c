#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <time.h>

// virtual sliding window settings
#define VSW_LEN 6
#define VSW ((1 << VSW_LEN) - 1)                // 63
#define VSW_SHIFT (VSW_LEN + VSW_LEN - 8)       // 4
#define VSW_ONE (1 << (VSW_LEN + VSW_LEN))      // 4096
#define VSW_HALF (1 << (VSW_LEN + VSW_LEN - 1)) // 2048

// rANS settings
#define RANS_BYTE_L (1u << 23) // 8388608
#define RANS_PROB_BITS 14
#define RANS_PROB_SCALE (1 << RANS_PROB_BITS)               // 16384
#define RANS_PROB_SCALEMINUSONE ((1 << RANS_PROB_BITS) - 1) // 16383
#define RANS_XMAX_SHIFT (23 - RANS_PROB_BITS + 8)           // 17
#define RANS_WSHIFT (RANS_PROB_BITS - VSW_LEN - VSW_LEN)    // 2

#define NUM_CONTEXT 16

struct Encoder
{
	uint32_t rans_state;
	uint8_t* rans_begin;
	uint8_t* bit_stream;
	int32_t* context_index;
	uint16_t** states;
	uint16_t* state;
};

struct Decoder
{
	uint32_t rans_state;
	uint8_t* dec_bytes;
	uint8_t* bitstream_bytes;
	int32_t* context_index;
	uint16_t* state;
};

size_t get_size_sequence(FILE* in_file)
{

	fseek(in_file, 0, SEEK_END);
	size_t in_size = ftell(in_file);
	fseek(in_file, 0, SEEK_SET);

	return in_size;
}

int cabrans_init_encoder(struct Encoder* cabrans_enc, FILE* in_file, FILE* file_context, size_t in_size)
{
	cabrans_enc->bit_stream = (uint8_t*)malloc(in_size * sizeof(uint8_t));
	cabrans_enc->context_index = (int32_t*)malloc(in_size * sizeof(int32_t));

	for (size_t i = 0; i < in_size; ++i)
	{
		fscanf(in_file, "%c", &cabrans_enc->bit_stream[i]);
		fscanf(file_context, "%i", &cabrans_enc->context_index[i]);
	}

	cabrans_enc->state = (uint16_t*)malloc(NUM_CONTEXT * sizeof(uint16_t));

	for (size_t i = 0; i < NUM_CONTEXT; ++i)
		cabrans_enc->state[i] = VSW_HALF;

	cabrans_enc->rans_state = RANS_BYTE_L;

	if ((cabrans_enc->states = (uint16_t**)calloc(in_size, sizeof(uint16_t*))))
		for (size_t i = 0; i < in_size; ++i)
		{
			if (!(cabrans_enc->states[i] = (uint16_t*)calloc(NUM_CONTEXT, sizeof(uint16_t))))
			{
				for (unsigned int j = 0; j < i; ++j)
					free(cabrans_enc->states[j]);
				free(cabrans_enc->states);
				fprintf(stderr, "Failed to allocate mamory.");
				return 1;
			}
		}
	else
	{
		printf("Failed to allocate mamory.");
		return 1;
	}
	return 0;
}

void cabrans_start_encode(struct Encoder* cabrans_enc, size_t in_size, FILE* out_file)
{
	clock_t start, end;
	start = clock();
	for (size_t i = 0; i < in_size; ++i)
	{
		int32_t context = cabrans_enc->context_index[i];
		if (context < 15)
		{
			cabrans_enc->states[i][context] = (cabrans_enc->state[context] << RANS_WSHIFT);

			if (cabrans_enc->bit_stream[i] == '0')
				cabrans_enc->state[context] -= (cabrans_enc->state[context]) >> VSW_LEN;
			else
				cabrans_enc->state[context] += (VSW_ONE - cabrans_enc->state[context]) >> VSW_LEN;
		}
		else {
			cabrans_enc->states[i][15] = (VSW_HALF << RANS_WSHIFT);
			//cabrans_enc->state[15] = VSW_HALF;
		}
	}

	uint8_t* out_buf = (uint8_t*)malloc(in_size);
	uint8_t* ptr = out_buf + in_size;
	for (size_t i = in_size; i > 0; --i)
	{
		int32_t symbol = cabrans_enc->bit_stream[i - 1];
		int32_t context = cabrans_enc->context_index[i - 1];

		if (context < 15)
		{
			if (symbol == '0')
			{
				if (cabrans_enc->rans_state >= (RANS_PROB_SCALE - cabrans_enc->states[i - 1][context]) << RANS_XMAX_SHIFT)
				{
					*--ptr = (uint8_t)(cabrans_enc->rans_state & 0xff);
					cabrans_enc->rans_state >>= 8;
				}
				cabrans_enc->rans_state = ((cabrans_enc->rans_state / (RANS_PROB_SCALE - cabrans_enc->states[i - 1][context])) << RANS_PROB_BITS) + (cabrans_enc->rans_state % (RANS_PROB_SCALE - cabrans_enc->states[i - 1][context]));
			}
			else
			{
				if (cabrans_enc->rans_state >= (cabrans_enc->states[i - 1][context]) << RANS_XMAX_SHIFT)
				{
					*--ptr = (uint8_t)(cabrans_enc->rans_state & 0xff);
					cabrans_enc->rans_state >>= 8;
				}
				cabrans_enc->rans_state = ((cabrans_enc->rans_state / cabrans_enc->states[i - 1][context]) << RANS_PROB_BITS) + RANS_PROB_SCALE - cabrans_enc->states[i - 1][context] + (cabrans_enc->rans_state % cabrans_enc->states[i - 1][context]);
			}
		}
		else
		{
			if (symbol == '0')
			{
				if (cabrans_enc->rans_state >= (RANS_PROB_SCALE - cabrans_enc->states[i - 1][15]) << RANS_XMAX_SHIFT)
				{
					*--ptr = (uint8_t)(cabrans_enc->rans_state & 0xff);
					cabrans_enc->rans_state >>= 8;
				}
				cabrans_enc->rans_state = ((cabrans_enc->rans_state / (RANS_PROB_SCALE - cabrans_enc->states[i - 1][15])) << RANS_PROB_BITS) + (cabrans_enc->rans_state % (RANS_PROB_SCALE - cabrans_enc->states[i - 1][15]));
			}
			else
			{
				if (cabrans_enc->rans_state >= (cabrans_enc->states[i - 1][15] << RANS_XMAX_SHIFT))
				{
					*--ptr = (uint8_t)(cabrans_enc->rans_state & 0xff);
					cabrans_enc->rans_state >>= 8;
				}
				cabrans_enc->rans_state = ((cabrans_enc->rans_state / cabrans_enc->states[i - 1][15]) << RANS_PROB_BITS) + RANS_PROB_SCALE - cabrans_enc->states[i - 1][15] + (cabrans_enc->rans_state % cabrans_enc->states[i - 1][15]);
			}
		}
	}


	ptr -= 4;
	ptr[0] = (uint8_t)(cabrans_enc->rans_state >> 0);
	ptr[1] = (uint8_t)(cabrans_enc->rans_state >> 8);
	ptr[2] = (uint8_t)(cabrans_enc->rans_state >> 16);
	ptr[3] = (uint8_t)(cabrans_enc->rans_state >> 24);
	cabrans_enc->rans_begin = ptr;
	end = clock();
	float time = (float)(end - start) / CLOCKS_PER_SEC;
	fprintf(stdout, "Time enc: %2.3f sec\n", time);

	int bitstreamsize = out_buf + in_size - cabrans_enc->rans_begin;
	fwrite(cabrans_enc->rans_begin, sizeof(unsigned char), bitstreamsize, out_file);

}

int cabrans_init_decode(struct Decoder* cabrans_dec, FILE* in_file, size_t in_size, size_t file_size, FILE* file_index) {
	cabrans_dec->dec_bytes = (uint8_t*)malloc(in_size);
	cabrans_dec->bitstream_bytes = (uint8_t*)malloc(file_size);
	cabrans_dec->context_index = (int32_t*)malloc(in_size * sizeof(int32_t));
	fread(cabrans_dec->bitstream_bytes, sizeof(uint8_t), file_size, in_file);
	for (size_t i = 0; i < in_size; ++i)
		fscanf(file_index, "%i", &cabrans_dec->context_index[i]);

	cabrans_dec->state = (uint16_t*)malloc(NUM_CONTEXT * sizeof(uint16_t));

	for (size_t i = 0; i < NUM_CONTEXT; ++i)
		cabrans_dec->state[i] = VSW_HALF;
	return 0;
}

void cabrans_start_decode(struct Decoder* cabrans_dec, size_t in_size, FILE* decoder_out) {
	clock_t start, end;
	uint8_t* ptr = cabrans_dec->bitstream_bytes;

	start = clock();
	cabrans_dec->rans_state = ptr[0];
	cabrans_dec->rans_state |= ptr[1] << 8;
	cabrans_dec->rans_state |= ptr[2] << 16;
	cabrans_dec->rans_state |= ptr[3] << 24;
	ptr += 4;

	for (size_t i = 0; i < in_size; ++i) {
		uint32_t cumcurr = cabrans_dec->rans_state & (RANS_PROB_SCALEMINUSONE);
		int32_t context = cabrans_dec->context_index[i];
		if (context < 15) {
			if (cumcurr < RANS_PROB_SCALE - (cabrans_dec->state[context] << RANS_WSHIFT)) {
				cabrans_dec->dec_bytes[i] = '0';
				cabrans_dec->rans_state = (RANS_PROB_SCALE - (cabrans_dec->state[context] << RANS_WSHIFT)) * (cabrans_dec->rans_state >> RANS_PROB_BITS) + cumcurr;
				cabrans_dec->state[context] -= (cabrans_dec->state[context]) >> VSW_LEN;
			}
			else {
				cabrans_dec->dec_bytes[i] = '1';
				cabrans_dec->rans_state = ((cabrans_dec->state[context] << RANS_WSHIFT)) * (cabrans_dec->rans_state >> RANS_PROB_BITS) + cumcurr - RANS_PROB_SCALE + (cabrans_dec->state[context] << RANS_WSHIFT);
				cabrans_dec->state[context] += (VSW_ONE - cabrans_dec->state[context]) >> VSW_LEN;
			}
		}
		else {
			if (cumcurr < RANS_PROB_SCALE - (VSW_HALF << RANS_WSHIFT)) {
				cabrans_dec->dec_bytes[i] = '0';
				cabrans_dec->rans_state = (RANS_PROB_SCALE - (VSW_HALF << RANS_WSHIFT)) * (cabrans_dec->rans_state >> RANS_PROB_BITS) + cumcurr;
			}
			else {
				cabrans_dec->dec_bytes[i] = '1';
				cabrans_dec->rans_state = ((VSW_HALF << RANS_WSHIFT)) * (cabrans_dec->rans_state >> RANS_PROB_BITS) + cumcurr - RANS_PROB_SCALE + (VSW_HALF << RANS_WSHIFT);
			}
		}
		if (cabrans_dec->rans_state < RANS_BYTE_L) {
			cabrans_dec->rans_state = (cabrans_dec->rans_state << 8) | *ptr++;
		}
	}
	end = clock();
	float time = (float)(end - start) / CLOCKS_PER_SEC;
	fprintf(stdout, "Time dec: %2.3f sec\n", time);
	fwrite(cabrans_dec->dec_bytes, sizeof(uint8_t), in_size, decoder_out);
}

void cabrans_clean_encode(struct Encoder* cabrans_enc, size_t in_size) {
	free(cabrans_enc->bit_stream);
	free(cabrans_enc->context_index);
	free(cabrans_enc->state);
	for (size_t i = 0; i < in_size; ++i)
		free(cabrans_enc->states[i]);
	free(cabrans_enc->states);
}

void cabrans_clean_decode(struct Decoder* cabrans_dec, size_t file_size, size_t in_size) {
	free(cabrans_dec->bitstream_bytes);
	free(cabrans_dec->dec_bytes);
	free(cabrans_dec->context_index);
	free(cabrans_dec->state);
}

int file_compare(FILE* ref_file, FILE* decode_file)
{
	char symb1, symb2;
	int not_equ = 0;
	int numof_encsymbol1;
	int i;
	double p;

	p = 0;
	numof_encsymbol1 = 0;

	for (i = 0; (!feof(ref_file)) && (!feof(decode_file)); ++i)
	{
		fscanf(ref_file, "%c", &symb1);
		fscanf(decode_file, "%c", &symb2);
		p = p + (symb1 - '0');
		numof_encsymbol1++;
		if (symb1 != symb2)
		{
			printf("%d: %c %c\n", i, symb1, symb2);
			not_equ++;
		}
	}

	if (i != numof_encsymbol1)
	{
		printf("files have different length\n");
	}

	return not_equ;
}


int main(int argc, char* argv[])
{
	//ENCODER
	FILE* in_file, * file_context, * out_file;
	if (in_file = fopen("bit_stream", "rb"))
	{
		if (!(out_file = fopen("result_stream", "wb")))
		{
			fclose(in_file);
			fprintf(stderr, "Failed to open out file.\n");
			return 1;
		}
	}
	else
	{
		fprintf(stderr, "Failed to open input file.\n");
		return 1;
	}

	if (!(file_context = fopen("context_index", "rb")))
	{
		fclose(in_file);
		fclose(out_file);
		fprintf(stderr, "Failed to open index file.\n");
		return 1;
	}

	size_t in_size = get_size_sequence(in_file);

	struct Encoder cabrans_enc;
	if (cabrans_init_encoder(&cabrans_enc, in_file, file_context, in_size)) {
		fclose(in_file);
		fclose(file_context);
		return 1;
	}

	fclose(in_file);
	fclose(file_context);
	cabrans_start_encode(&cabrans_enc, in_size, out_file);
	fclose(out_file);
	cabrans_clean_encode(&cabrans_enc, in_size);


	//DECODER
	if (in_file = fopen("result_stream", "rb"))
	{
		if (!(out_file = fopen("dec_stream", "wb")))
		{
			fclose(in_file);
			fprintf(stderr, "Failed to open out file.\n");
			return 1;
		}
	}
	else
	{
		fprintf(stderr, "Failed to open input file.\n");
		return 1;
	}

	if (!(file_context = fopen("context_index", "rb")))
	{
		fclose(in_file);
		fclose(out_file);
		fprintf(stderr, "Failed to open index file.\n");
		return 1;
	}

	struct Decoder cabrans_dec;
	size_t result_size = get_size_sequence(in_file);
	if (cabrans_init_decode(&cabrans_dec, in_file, in_size, result_size, file_context)) {
		fclose(in_file);
		fclose(file_context);
		return 1;
	}
	fclose(in_file);
	fclose(file_context);

	cabrans_start_decode(&cabrans_dec, in_size, out_file);

	fclose(out_file);
	cabrans_clean_decode(&cabrans_dec, result_size, in_size);


	//CHECK
	FILE* ref_file, * dec_file;
	if (!(ref_file = fopen("bit_stream", "rb")))
	{
		fprintf(stderr, "Failed to open index file.\n");
		return 1;
	}
	if (!(dec_file = fopen("dec_stream", "rb")))
	{
		fprintf(stderr, "Failed to open index file.\n");
		return 1;
	}
	printf("differences = %d\n", file_compare(ref_file, dec_file));
	fclose(ref_file);
	fclose(dec_file);

	return 0;
}