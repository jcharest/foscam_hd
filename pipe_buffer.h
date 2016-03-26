/*
 * PipeBuffer.h
 *
 *  Created on: Mar 25, 2016
 *      Author: jonathan
 */

#ifndef PIPE_BUFFER_H_
#define PIPE_BUFFER_H_

#include <condition_variable>
#include <deque>
#include <mutex>

namespace foscam_hd {

class PipeBuffer {
 public:
  void push(const uint8_t * data, size_t size);
  bool empty() const;
  size_t read_available() const;
  size_t try_pop(uint8_t * data, size_t max_size);
  size_t wait_and_pop(uint8_t * data, size_t max_size,
                      std::chrono::milliseconds timeout);

 private:
  std::deque<uint8_t> queue_;
  mutable std::mutex mutex_;
  std::condition_variable data_available_;
};

}  // namespace foscam_hd

#endif  // PIPE_BUFFER_H_
