#ifndef VOCO_H
#define VOCO_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOCO_MAGIC "V1"
#define VOCO_MAGIC_LENGTH 2
#define VOCO_MAX_FIELDS 80

#if defined(__GNUC__)
#define VOCO_UNUSED __attribute__((unused))
#else
#define VOCO_UNUSED
#endif

struct voco_field {
  const char *data;
  size_t length;
};

struct voco_message {
  int count;
  struct voco_field fields[VOCO_MAX_FIELDS];
};

typedef int (*voco_append_fn)(
    void *context, const char *text, size_t length);

static VOCO_UNUSED int voco_byte_is_literal(unsigned char byte) {
  return (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') ||
         byte == '-' || byte == '_' || byte == '.' || byte == '~';
}

static VOCO_UNUSED int voco_parse_size(
    const char *text, size_t length, size_t *offset, size_t *out) {
  size_t value = 0;
  size_t i = *offset;
  if (i >= length || text[i] < '0' || text[i] > '9') return 0;

  while (i < length && text[i] >= '0' && text[i] <= '9') {
    size_t digit = (size_t)(text[i] - '0');
    if (value > ((size_t)-1 - digit) / 10) return 0;
    value = value * 10 + digit;
    i++;
  }
  if (i >= length || text[i] != ':') return 0;

  *offset = i + 1;
  *out = value;
  return 1;
}

static VOCO_UNUSED int voco_parse(
    const char *text, struct voco_message *message) {
  size_t length;
  size_t offset = VOCO_MAGIC_LENGTH;

  if (!text || !message) return 0;
  length = strlen(text);
  if (length < VOCO_MAGIC_LENGTH ||
      memcmp(text, VOCO_MAGIC, VOCO_MAGIC_LENGTH) != 0) {
    return 0;
  }

  memset(message, 0, sizeof(*message));
  while (offset < length) {
    size_t field_length;
    if (message->count >= VOCO_MAX_FIELDS ||
        !voco_parse_size(text, length, &offset, &field_length) ||
        field_length > length - offset) {
      return 0;
    }
    message->fields[message->count].data = text + offset;
    message->fields[message->count].length = field_length;
    message->count++;
    offset += field_length;
  }

  return message->count > 0;
}

static VOCO_UNUSED int voco_field_equals(
    const struct voco_field *field, const char *text) {
  size_t length = strlen(text);
  return field && field->length == length &&
    memcmp(field->data, text, length) == 0;
}

static VOCO_UNUSED char *voco_field_cstr(const struct voco_field *field) {
  char *text;
  size_t used = 0;
  if (!field) return NULL;
  text = (char *)malloc(field->length + 1);
  if (!text) return NULL;
  for (size_t i = 0; i < field->length; i++) {
    unsigned char byte = (unsigned char)field->data[i];
    if (byte == '%' && i + 2 < field->length) {
      int hi = field->data[i + 1];
      int lo = field->data[i + 2];
      if (hi >= '0' && hi <= '9') hi -= '0';
      else if (hi >= 'A' && hi <= 'F') hi = hi - 'A' + 10;
      else if (hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10;
      else hi = -1;
      if (lo >= '0' && lo <= '9') lo -= '0';
      else if (lo >= 'A' && lo <= 'F') lo = lo - 'A' + 10;
      else if (lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10;
      else lo = -1;
      if (hi >= 0 && lo >= 0) {
        text[used++] = (char)((hi << 4) | lo);
        i += 2;
        continue;
      }
    }
    text[used++] = (char)byte;
  }
  text[used] = '\0';
  return text;
}

static VOCO_UNUSED int voco_field_long(
    const struct voco_field *field, long min, long max, long *out) {
  char text[64];
  char *end;
  long value;

  if (!field || field->length == 0 || field->length >= sizeof(text)) return 0;
  memcpy(text, field->data, field->length);
  text[field->length] = '\0';
  value = strtol(text, &end, 10);
  if (end == text || *end != '\0' || value < min || value > max) return 0;

  *out = value;
  return 1;
}

static VOCO_UNUSED int voco_write_field(
    void *context, voco_append_fn append, const char *text, size_t length) {
  char prefix[32];
  int prefix_length = snprintf(prefix, sizeof(prefix), "%zu:", length);
  return prefix_length >= 0 && (size_t)prefix_length < sizeof(prefix) &&
    append(context, prefix, (size_t)prefix_length) &&
    append(context, text, length);
}

static VOCO_UNUSED int voco_write_text_field(
    void *context, voco_append_fn append, const char *text) {
  static const char hex[] = "0123456789ABCDEF";
  size_t length = 0;

  if (!text) text = "";
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    length += voco_byte_is_literal(*p) ? 1 : 3;
  }
  {
    char prefix[32];
    int prefix_length = snprintf(prefix, sizeof(prefix), "%zu:", length);
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix) ||
        !append(context, prefix, (size_t)prefix_length)) {
      return 0;
    }
  }
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    unsigned char byte = *p;
    if (voco_byte_is_literal(byte)) {
      if (!append(context, (const char *)&byte, 1)) return 0;
    } else {
      char encoded[3];
      encoded[0] = '%';
      encoded[1] = hex[byte >> 4];
      encoded[2] = hex[byte & 0x0f];
      if (!append(context, encoded, sizeof(encoded))) return 0;
    }
  }
  return 1;
}

static VOCO_UNUSED int voco_write_int_field(
    void *context, voco_append_fn append, int value) {
  char text[32];
  int length = snprintf(text, sizeof(text), "%d", value);
  return length >= 0 && (size_t)length < sizeof(text) &&
    voco_write_field(context, append, text, (size_t)length);
}

static VOCO_UNUSED int voco_write_u64_field(
    void *context, voco_append_fn append, unsigned long long value) {
  char text[32];
  int length = snprintf(text, sizeof(text), "%llu", value);
  return length >= 0 && (size_t)length < sizeof(text) &&
    voco_write_field(context, append, text, (size_t)length);
}

static VOCO_UNUSED int voco_write_frame_header(
    void *context,
    voco_append_fn append,
    const char *vocab,
    const char *command,
    const char *type) {
  return append(context, VOCO_MAGIC, VOCO_MAGIC_LENGTH) &&
    voco_write_text_field(context, append, vocab) &&
    voco_write_text_field(context, append, command) &&
    voco_write_text_field(context, append, type);
}

#undef VOCO_UNUSED

#endif /* VOCO_H */
