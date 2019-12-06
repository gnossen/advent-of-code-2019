#include "1202.h"
#include <stdlib.h>
#include <string.h>

static const uint64_t k_add_op = 1;
static const uint64_t k_mult_op = 2;
static const uint64_t k_input_op = 3;
static const uint64_t k_output_op = 4;
static const uint64_t k_halt_op = 99;

static const size_t k_max_program_size = 1 << 14;
static const size_t k_io_size = 1 << 10;

// Can be used on both programs *and* processes.
static void advance(uint64_t *data, size_t len, uint64_t **ip, size_t steps) {
  if ((*ip - data) + steps > len + 1) {
    fprintf(stderr,
            "Malformed program. Attempt to advance to position %d when buffer "
            "is only of size %d.\n",
            (*ip - data) + steps, len);
    exit(1);
  }
  (*ip) += steps;
}

static int is_numerical(char c) { return c >= '0' && c <= '9'; }

uint64_t *program_end(program_t program) { return program.data + program.len; }

void pretty_print_program(FILE *f, program_t program) {
  uint64_t *ip = program.data;
  do {
    if (*ip == k_halt_op) {
      advance(program.data, program.len, &ip, 1);
      fprintf(f, "%d\n", *(ip - 1));
    } else if (*ip == k_input_op || *ip == k_output_op) {
      advance(program.data, program.len, &ip, 2);
      fprintf(f, "%d, %d\n", *(ip - 2), *(ip - 1));
    } else if (*ip == k_add_op || *ip == k_mult_op) {
      advance(program.data, program.len, &ip, 4);
      fprintf(f, "%d, %d, %d, %d\n", *(ip - 4), *(ip - 3), *(ip - 2),
              *(ip - 1));
    } else {
      size_t upper = MIN(4, program.len - (ip - program.data));
      for (size_t i = 0; i < upper; ++i) {
        advance(program.data, program.len, &ip, 1);
        fprintf(f, "%d", *(ip - 1));
        if (ip != program_end(program)) {
          fprintf(f, ", ");
        }
      }
      fprintf(f, "\n");
    }
  } while (ip < program_end(program));
}

program_t program_from_text_file(FILE *f) {
  uint64_t *data = malloc(sizeof(uint64_t) * k_max_program_size);
  uint64_t *rp = data;
  char buffer[k_buffer_size];
  char *bp = buffer;
  int cur = '0';
  size_t len = 0;
  while (cur != EOF) {
    // Read number.
    while (1) {
      cur = fgetc(f);
      if (is_numerical(cur)) {
        *(bp++) = (char)cur;
      } else {
        // Okay to drop this character.
        *bp = '\0';
        *(rp++) = atoi(buffer);
        bp = buffer;
        break;
      }
    }

    if (cur == EOF)
      break;

    // Skip over non-meaningful characters
    while (1) {
      cur = fgetc(f);
      if (cur == EOF) {
        break;
      } else if (is_numerical(cur)) {
        *(bp++) = (char)cur;
        break;
      }
    }
  }
  // NOTE: Shrink down to the smallest possible size for maximum performance.
  size_t program_len = rp - data;
  data = realloc(data, sizeof(uint64_t) * program_len);
  program_t program = {
      .data = data, .len = program_len, .buffer_len = program_len};
  return program;
}

program_t program_from_text_filepath(const char *path) {
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    fprintf(stderr, "Unable to open %s.\n", path);
    exit(1);
  }
  program_t program = program_from_text_file(f);
  fclose(f);
  return program;
}

static uint64_t add_op(process_t *process, uint64_t a, uint64_t b) {
  return a + b;
}

static uint64_t mult_op(process_t *process, uint64_t a, uint64_t b) {
  return a * b;
}

typedef uint64_t (*binary_op)(process_t *process, uint64_t a, uint64_t b);

static void perform_binary_op(process_t *process, binary_op op) {
  advance(process->data, process->len, &(process->ip), 4);
  if (*(process->ip - 1) >= process->len) {
    fprintf(stderr,
            "Attempt to modify location %d from instruction at location %d.",
            *(process->ip - 1), process->ip - 4);
    exit(1);
  }
  *(process->data + *(process->ip - 1)) =
      op(process, *(process->data + *(process->ip - 3)),
         *(process->data + *(process->ip - 2)));
}

process_status execute(process_t *process) {
  while (process->ip < process->data + process->len) {
    if (*(process->ip) == k_add_op) {
      perform_binary_op(process, add_op);
    } else if (*(process->ip) == k_mult_op) {
      perform_binary_op(process, mult_op);
    } else if (*(process->ip) == k_input_op) {
      if (buffer_empty(process->input)) {
        return AWAITING_READ;
      }
      advance(process->data, process->len, &(process->ip), 2);
      *(process->data + *(process->ip - 1)) = buffer_read(process->input);
    } else if (*(process->ip) == k_output_op) {
      if (buffer_full(process->output)) {
        return AWAITING_WRITE;
      }
      advance(process->data, process->len, &(process->ip), 2);
      buffer_write(process->output, *(process->data + *(process->ip - 1)));
    } else if (*(process->ip) == k_halt_op) {
      return HALTED;
    } else {
      fprintf(stderr, "Unrecognized opcode %d at location %d.\n",
              *(process->ip), process->ip - process->data);
      exit(1);
    }
  }
  return HALTED;
}

process_t *instantiate_process_from_buffer(program_t program, uint64_t *buffer,
                                           size_t buffer_len) {
  if (buffer_len < program.len) {
    fprintf(stderr,
            "Attempt to execute program of length %d in buffer of length %d.\n",
            program.len, buffer_len);
    exit(1);
  }
  memcpy(buffer, program.data, sizeof(uint64_t) * program.len);
  process_t *process = malloc(sizeof(process_t));
  process->data = buffer;
  process->len = program.len;
  process->buffer_len = buffer_len;
  process->ip = buffer;
  process->input = make_buffer(k_io_size);
  process->output = make_buffer(k_io_size);
  return process;
}

void reset_process(const program_t program, process_t *process) {
  memcpy(process->data, program.data, sizeof(uint64_t) * program.len);
  process->ip = process->data;
  buffer_clear(process->input);
  buffer_clear(process->output);
}

// NOTE: It is the caller's responsibility to free the memory in this process.
process_t *instantiate_process(program_t program) {
  uint64_t *buffer = malloc(sizeof(uint64_t) * program.len);
  return instantiate_process_from_buffer(program, buffer, program.len);
}

void destroy_process(process_t *process) {
  free(process->data);
  destroy_buffer(process->input);
  destroy_buffer(process->output);
  free(process);
}

void destroy_program(program_t program) { free(program.data); }

buffer_t *make_buffer(size_t size) {
  uint64_t *data = malloc(sizeof(uint64_t) * size);
  buffer_t *buffer = malloc(sizeof(buffer_t));
  buffer->data = data;
  buffer->read_index = 0;
  buffer->write_index = 0;
  buffer->len = size;
  return buffer;
}

void destroy_buffer(buffer_t *buffer) {
  free(buffer->data);
  free(buffer);
}

bool buffer_empty(const buffer_t *buffer) {
  return buffer->read_index == buffer->write_index;
}

bool buffer_full(const buffer_t *buffer) {
  return buffer->write_index - buffer->read_index == buffer->len;
}

void buffer_clear(buffer_t *buffer) {
  buffer->read_index = 0;
  buffer->write_index = 0;
}

uint64_t buffer_read(buffer_t *buffer) {
  return buffer->data[buffer->read_index++ % buffer->len];
}

void buffer_write(buffer_t *buffer, uint64_t val) {
  buffer->data[buffer->write_index++ % buffer->len] = val;
}