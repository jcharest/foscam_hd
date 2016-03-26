#include <pipe_buffer.h>

namespace foscam_hd {

void PipeBuffer::push(const uint8_t * data, size_t size) {
  std::unique_lock<std::mutex> lock(mutex_);
  queue_.insert(queue_.end(), data, data + size);
  lock.unlock();
  data_available_.notify_one();
}

bool PipeBuffer::empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty();
}

size_t PipeBuffer::read_available() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

size_t PipeBuffer::try_pop(uint8_t * data, size_t max_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    return 0;
  }

  size_t size = std::min(queue_.size(), max_size);
  std::copy(queue_.begin(), queue_.begin() + size, data);
  queue_.erase(queue_.begin(), queue_.begin() + size);

  return size;
}

size_t PipeBuffer::wait_and_pop(uint8_t * data, size_t max_size,
                                std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (queue_.empty()) {
    data_available_.wait_for(lock, timeout);
  }

  size_t size = std::min(queue_.size(), max_size);
  std::copy(queue_.begin(), queue_.begin() + size, data);
  queue_.erase(queue_.begin(), queue_.begin() + size);

  return size;
}

}  // namespace foscam_hd
