#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cuda.h>
#include <cuda_device_runtime_api.h>
#include <cuda_runtime.h>
#include <cuda_runtime_api.h>
#include <device_atomic_functions.h>
#include <driver_types.h>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <vector_types.h>

// align up to the nearest multiple of 512
#define ALIGN_UP_512(x) (((x) + 511) & ~511)
#define blockSize 256
#define GPU_CHUNK_COUNT 15 * 1024 * 1024
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

std::atomic<int> buf_state[2];
size_t buf_read_bytes[2];

struct TradeBinData {
  int64_t price, qty, timestamp;
  // the unit is 0.01 USDT
  // the unit is 1 satoshi, 1 satoshi == 100,000,000 bitcoin
  bool is_sell;
} __attribute__((packed));

struct file_buffer {
  TradeBinData *buffer_head;
  size_t buf_size;
  size_t actual_read_size;
  bool is_success;
  std::vector<size_t> change_file_point;
  std::vector<std::string> finished_names;
};

struct GpuResult {
  unsigned long long total_buy, total_sell;
  double total_amount;
};

struct All_ptr {
  file_buffer buffer[2];

  long long *host_price;
  long long *host_qty;
  bool *host_is_sell;

  long long *d_p;
  long long *d_q;
  bool *d_is;

  GpuResult *gpu_out_host;
  GpuResult *gpu_out_dev;
};

void pin_to_core(int core_id) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);

  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

void apply_for_host_RAM(size_t host_buffer_size, file_buffer *host_buffer,
                        long long **h_p, long long **h_q, bool **h_is,
                        GpuResult **gpu_out_host) {
  // the function need to apply for 2 host buffer(every buffer is size bytes)
  // and 3 array
  cudaHostAlloc((void **)&(host_buffer[0].buffer_head), host_buffer_size,
                cudaHostAllocDefault);
  cudaHostAlloc((void **)&(host_buffer[1].buffer_head), host_buffer_size,
                cudaHostAllocDefault);

  cudaHostAlloc((void **)h_p, GPU_CHUNK_COUNT * sizeof(long long),
                cudaHostAllocDefault);
  cudaHostAlloc((void **)h_q, GPU_CHUNK_COUNT * sizeof(long long),
                cudaHostAllocDefault);
  cudaHostAlloc((void **)h_is, GPU_CHUNK_COUNT * sizeof(bool),
                cudaHostAllocDefault);

  cudaHostAlloc((void **)gpu_out_host, sizeof(GpuResult), cudaHostAllocDefault);
}

void apply_for_device_VRAM(long long **d_p, long long **d_q, bool **d_is,
                           GpuResult **gpu_out_dev) {
  cudaMalloc((void **)d_p, GPU_CHUNK_COUNT * sizeof(long long));
  cudaMalloc((void **)d_q, GPU_CHUNK_COUNT * sizeof(long long));
  cudaMalloc((void **)d_is, GPU_CHUNK_COUNT * sizeof(bool));

  cudaMalloc((void **)gpu_out_dev, sizeof(GpuResult));
}
__global__ void sum_data(long long *price, long long *qty, bool *is_sell_arr,
                         size_t n, GpuResult *out) {

  size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
  size_t stride = blockDim.x * gridDim.x;

  long long local_buy = 0;
  long long local_sell = 0;
  double local_amount = 0;

  for (size_t i = idx; i < n; i += stride) {
    long long p = price[i];
    long long q = qty[i];
    bool is_sell = is_sell_arr[i];

    long long sell_val = is_sell ? q : 0;
    long long buy_val = is_sell ? 0 : q;
    double amount = double(q) * double(p) * 1e-16;

    local_buy += buy_val;
    local_sell += sell_val;
    local_amount += amount;
  }
  atomicAdd(&out->total_buy, local_buy);
  atomicAdd(&out->total_sell, local_sell);
  atomicAdd(&out->total_amount, local_amount);
}

void io_worker(All_ptr *work_ptr, size_t BUFFER_CAPACITY,
               std::vector<std::string> file_path_list) {

  // initialize
  pin_to_core(0);
  int buf_idx = 0;
  // buf_state = 0 empty, 1 fill, -1 finish computing
  buf_state[0].store(0);
  buf_state[1].store(0);
  size_t buf_offset = 0;

  for (int i = 0; i < file_path_list.size(); i++) {
    int fd = open(file_path_list[i].c_str(), O_RDONLY);
    if (fd == -1) {
      std::cerr << "Open failed for file: " << file_path_list[i] << std::endl;
      continue;
    }

    struct stat sb;
    fstat(fd, &sb);
    size_t file_remain_bytes = sb.st_size;
    size_t file_offset = 0;
    while (file_remain_bytes > 0) {
      while (buf_state[buf_idx].load() != 0)
        ;
      size_t to_read = MIN(BUFFER_CAPACITY - buf_offset, file_remain_bytes);
      ssize_t res =
          pread(fd, (char *)work_ptr->buffer[buf_idx].buffer_head + buf_offset,
                to_read, file_offset);

      if (res <= 0) {
        printf("read failed\n");
        break;
      }
      buf_offset += res;
      file_offset += res;
      file_remain_bytes = sb.st_size - file_offset;

      if (file_remain_bytes == 0) {
        work_ptr->buffer[buf_idx].change_file_point.emplace_back(buf_offset /
                                                                 25);
        work_ptr->buffer[buf_idx].finished_names.push_back(file_path_list[i]);
      }

      if (buf_offset == BUFFER_CAPACITY) {
        // the buffer is full
        work_ptr->buffer[buf_idx].actual_read_size = BUFFER_CAPACITY;
        work_ptr->buffer[buf_idx].is_success = true;
        buf_state[buf_idx].store(1);
        buf_idx = 1 - buf_idx;
        buf_offset = 0;
      }
    }
    close(fd);
  }
  // finally, the buffer is not full
  if (buf_offset > 0) {
    work_ptr->buffer[buf_idx].actual_read_size = buf_offset;
    buf_state[buf_idx].store(1);
  }
  while (buf_state[1].load() != 0 || buf_state[0].load() != 0)
    ;
  buf_state[0].store(-1);
  buf_state[1].store(-1);
}

void print_a_file_result(All_ptr *work_ptr, std::string file_name) {
  cudaMemcpy(work_ptr->gpu_out_host, work_ptr->gpu_out_dev, sizeof(GpuResult),
             cudaMemcpyDeviceToHost);
  std::cout << "==================================================="
            << std::endl;
  std::cout << file_name << std::endl;
  std::cout << std::fixed << std::setprecision(6);
  std::cout << (double)work_ptr->gpu_out_host->total_buy * 1e-8 << std::endl;
  std::cout << (double)work_ptr->gpu_out_host->total_sell * 1e-8 << std::endl;
  std::cout << work_ptr->gpu_out_host->total_amount << std::endl;
  cudaMemset(work_ptr->gpu_out_dev, 0, sizeof(GpuResult));
}

void process_sub_chunk(All_ptr *work_ptr, int buf_idx, size_t *start,
                       size_t *end) {
  // this function take the responsibility to compute one range data
  // the range is included in a buffer

  size_t current_offset = *start;
  while (current_offset < *end) {

    size_t batch = MIN(GPU_CHUNK_COUNT, *end - current_offset);
#pragma omp parallel for
    for (size_t k = 0; k < batch; k++) {
      work_ptr->host_price[k] =
          work_ptr->buffer[buf_idx % 2].buffer_head[current_offset + k].price;
      work_ptr->host_qty[k] =
          work_ptr->buffer[buf_idx % 2].buffer_head[current_offset + k].qty;
      work_ptr->host_is_sell[k] =
          work_ptr->buffer[buf_idx % 2].buffer_head[current_offset + k].is_sell;
    }
    cudaMemcpyAsync(work_ptr->d_p, work_ptr->host_price, batch * 8,
                    cudaMemcpyHostToDevice, 0);
    cudaMemcpyAsync(work_ptr->d_q, work_ptr->host_qty, batch * 8,
                    cudaMemcpyHostToDevice, 0);
    cudaMemcpyAsync(work_ptr->d_is, work_ptr->host_is_sell, batch * 1,
                    cudaMemcpyHostToDevice, 0);
    int gridSize = (batch + blockSize - 1) / blockSize;

    sum_data<<<gridSize, blockSize>>>(work_ptr->d_p, work_ptr->d_q,
                                      work_ptr->d_is, batch,
                                      work_ptr->gpu_out_dev);
    *start += batch;
    current_offset += batch;
  }
  cudaDeviceSynchronize();
}
void data_chunk_process(All_ptr *work_ptr, int buf_idx,
                        size_t BUFFER_CAPACITY) {

  // this function can deal 1 buffer data

  size_t current_offset = 0;
  for (int i = 0; i < work_ptr->buffer[buf_idx].change_file_point.size(); i++) {
    process_sub_chunk(
        work_ptr, buf_idx, &current_offset,
        (size_t *)&(work_ptr->buffer[buf_idx].change_file_point[i]));
    std::string file_name = work_ptr->buffer[buf_idx].finished_names[i];
    print_a_file_result(work_ptr, file_name);
  }
  size_t total_record = work_ptr->buffer[buf_idx].actual_read_size / 25;
  process_sub_chunk(work_ptr, buf_idx, &current_offset, &total_record);
  work_ptr->buffer[buf_idx].change_file_point.clear();
  work_ptr->buffer[buf_idx].finished_names.clear();
  cudaDeviceSynchronize();
}

int main(int argc, char *argv[]) {

  pin_to_core(1);
  std::vector<std::string> file_path_list;
  std::string one_file_absolute_path;

  // apply for the necessary RAM
  size_t BUFFER_CAPACITY = 80000 * 512 * 25;

  All_ptr work_ptr;
  work_ptr.buffer[0].buf_size = BUFFER_CAPACITY;
  work_ptr.buffer[1].buf_size = BUFFER_CAPACITY;
  apply_for_host_RAM(BUFFER_CAPACITY, work_ptr.buffer, &(work_ptr.host_price),
                     &(work_ptr.host_qty), &(work_ptr.host_is_sell),
                     &(work_ptr.gpu_out_host));

  // apply for the necessary VRAM
  apply_for_device_VRAM(&work_ptr.d_p, &work_ptr.d_q, &work_ptr.d_is,
                        &work_ptr.gpu_out_dev);

  while (std::getline(std::cin, one_file_absolute_path)) {
    // read the file absolute path from the pipe
    // and input to the file_path_list
    if (!one_file_absolute_path.empty()) {
      file_path_list.push_back(one_file_absolute_path);
    }
  }

  std::thread t(io_worker, &work_ptr, BUFFER_CAPACITY, file_path_list);

  int buf_idx = 0;
  while (true) {
    while (buf_state[buf_idx].load() == 0)
      ;
    if (buf_state[buf_idx].load() == -1) {
      break;
    }
    //======================================
    data_chunk_process(&work_ptr, buf_idx, BUFFER_CAPACITY);
    buf_state[buf_idx].store(0);
    buf_idx = 1 - buf_idx;
  }

  t.join();

  // free the VRAM and RAM
  cudaFree(work_ptr.gpu_out_dev);
  cudaFree(work_ptr.d_p);
  cudaFree(work_ptr.d_q);
  cudaFree(work_ptr.d_is);
  cudaFreeHost(work_ptr.gpu_out_host);
  cudaFreeHost(work_ptr.host_price);
  cudaFreeHost(work_ptr.host_qty);
  cudaFreeHost(work_ptr.host_is_sell);
  return 0;
}
