#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

struct TradeBinData {
  int64_t price, qty, timestamp;
  // the unit is 0.01 USDT
  // the unit is 1 satoshi, 1 satoshi == 100,000,000 bitcoin
  bool is_sell;
} __attribute__((packed));

TradeBinData convert(const char *line_start, const char *line_end) {
  /*
   * This function traverses the entire line of data only once from start to
   * finish via the pointer line_start and converts it into a structure
   * TradeBinData
   */
  TradeBinData BinData = {0, 0, 0, false};
  size_t comma_num = 0;
  int64_t val = 0;
  bool bool_val;
  int64_t fraction_count = 0;
  bool in_fraction = false;
  while (true) {
    if (line_end != line_start && *line_start != '\n') {
      if ('0' <= *line_start && *line_start <= '9') {
        val = val * 10 + (*line_start - '0');
        if (in_fraction) {
          fraction_count++;
        }
      }
      if (*line_start == ',') {
        comma_num += 1;

        switch (comma_num) {
        case 1:
          val = 0;
          break;
        case 2:
          if (fraction_count - 8 == 0) {
            BinData.price = val;
            val = 0;
            break;
          } else if (fraction_count - 8 >
                     0) { // if the fraction_count is more than 8
            switch (fraction_count - 8) {
            case 1:
              BinData.price = val / 10;
              val = 0;
              break;
            case 2:
              BinData.price = val / 100;
              val = 0;
              break;
            case 3:
              BinData.price = val / 1000;
              val = 0;
              break;
            case 4:
              BinData.price = val / 10000;
              val = 0;
              break;
            case 5:
              BinData.price = val / 100000;
              val = 0;
              break;
            case 6:
              BinData.price = val / 1000000;
              val = 0;
              break;
            case 7:
              BinData.price = val / 10000000;
              val = 0;
              break;
            case 8:
              BinData.price = val / 100000000;
              val = 0;
              break;
            }
            val = 0;
            break;
          } else { // if the fraction_count is less than 8
            switch (fraction_count - 8) {
            case -1:
              BinData.price = val * 10;
              val = 0;
              break;
            case -2:
              BinData.price = val * 100;
              val = 0;
              break;
            case -3:
              BinData.price = val * 1000;
              val = 0;
              break;
            case -4:
              BinData.price = val * 10000;
              val = 0;
              break;
            case -5:
              BinData.price = val * 100000;
              val = 0;
              break;
            case -6:
              BinData.price = val * 1000000;
              val = 0;
              break;
            case -7:
              BinData.price = val * 10000000;
              val = 0;
              break;
            case -8:
              BinData.price = val * 100000000;
              val = 0;
              break;
            default:
              std::cout << "the fraction_count of price have something worng";
            }
            val = 0;
            break;
          }
        case 3: {
          switch (8 - fraction_count) {
          case 0:
            BinData.qty = val;
            break;
          case 1:
            BinData.qty = val * 10;
            break;
          case 2:
            BinData.qty = val * 100;
            break;
          case 3:
            BinData.qty = val * 1000;
            break;
          case 4:
            BinData.qty = val * 10000;
            break;
          case 5:
            BinData.qty = val * 100000;
            break;
          case 6:
            BinData.qty = val * 1000000;
            break;
          case 7:
            BinData.qty = val * 10000000;
            break;
          case 8:
            BinData.qty = val * 100000000;
            break;
          }
          val = 0;
          break;
        }
        case 4:
          val = 0;
          break;
        case 5:
          BinData.timestamp = val;
          val = 0;
          break;
        }
        in_fraction = false;
        fraction_count = 0;
      } else if (*line_start == '.') {
        in_fraction = true;
      } else if (*line_start == 'T' && comma_num == 5) {
        BinData.is_sell = true;
        comma_num = 0;
        return BinData;
      } else if (*line_start == 'F' && comma_num == 5) {
        BinData.is_sell = false;
        comma_num = 0;
        return BinData;
      }
    }

    else {
      // finish reading the entire line
      return BinData;
    }
    line_start++;
  }
}
int main(int argc, char *argv[]) {

  const size_t BUFFER_SIZE = 1024 * 1024;
  char buffer[BUFFER_SIZE]; // Allocate 64KB of stack memory as a buffer
  TradeBinData out_buffer[40000];
  size_t out_idx = 0;
  // count of characters truncated in the previous operation
  size_t left_over = 0;

  // main loop
  while (1) {

    // read the data from stdin
    size_t bytes_read =
        fread(buffer + left_over, 1, BUFFER_SIZE - left_over, stdin);
    if (bytes_read == 0) {
      break; // finish reading from the document
    }

    // handle the text-only data stored in the buffer
    const char *buf_start = &buffer[0];
    const char *buf_end = buf_start + bytes_read + left_over;
    const char *line_start = buf_start;

    while (line_start < buf_end) {
      const char *line_end =
          (char *)std::memchr(line_start, '\n', buf_end - line_start);
      if (line_end != nullptr) {
        out_buffer[out_idx] = convert(line_start, line_end);
        out_idx++;
        left_over = 0;
        if (out_idx == 40000) {
          fwrite(out_buffer, sizeof(TradeBinData), out_idx, stdout);
          out_idx = 0;
        }
        if (line_end < buf_end)
          line_start = line_end + 1;
        else
          break;
      } else {
        left_over = buf_end - line_start;
        if (left_over != 0) {
          std::memmove(buffer, line_start, left_over);
        }
        break;
      }
    }
  }
  if (left_over > 0) {
    out_buffer[out_idx] = convert(buffer, buffer + left_over);
    out_idx++;
    fwrite(out_buffer, sizeof(TradeBinData), out_idx, stdout);
    out_idx = 0;
  }
  if (out_idx > 0) {
    fwrite(out_buffer, sizeof(TradeBinData), out_idx, stdout);
    out_idx = 0;
  }
  return 0;
}
