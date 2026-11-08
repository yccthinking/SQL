//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);

  // 是否已经找到一个可以淘汰的候选帧。
  bool found = false;

  // 当前选中的候选帧编号，-1 表示还没选中。
  frame_id_t victim = -1;

  // 当前候选帧的访问次数是否不足 K 次。
  bool victim_less_k = false;

  // 当前候选帧用于比较的时间戳。
  // found 为 false 时，这个初始值不参与比较。
  size_t oldest_timestamp = 0;

  // 遍历所有帧记录：
  // id 是帧编号，frame 是该帧的访问历史和淘汰状态。
  for (const auto &[id, frame] : frames_) {
    // 不允许淘汰的帧直接跳过。
    if (!frame.evictable_) {
      continue;
    }

    // 判断当前帧的访问次数是否不足 K 次。
    bool less_k = frame.history_.size() < k_;

    // 历史记录按时间从旧到新排列，取最前面的时间。
    // 不足 K 次：它是首次访问时间。
    // 达到 K 次：它是倒数第 K 次访问时间。
    size_t timestamp = frame.history_.front();

    // 满足下面任意一种情况，就将当前帧选为新的候选：
    //
    // 1. 还没有候选帧。
    // 2. 当前帧不足 K 次，而原候选帧已经达到 K 次。
    // 3. 两者都不足 K 次，或者都达到 K 次，
    //    且当前帧的比较时间更早。
    if (!found || (less_k && !victim_less_k) || (less_k == victim_less_k && timestamp < oldest_timestamp)) {
      // 保存当前候选帧的信息，供后续帧进行比较。
      found = true;
      victim = id;
      victim_less_k = less_k;
      oldest_timestamp = timestamp;
    }
  }

  // 遍历完成后，仍然没有可淘汰的帧，返回失败。
  if (!found) {
    return false;
  }

  // 通过输出参数，将淘汰的帧编号交给调用者。
  *frame_id = victim;

  // 删除该帧的全部记录，包括访问历史。
  frames_.erase(victim);

  // 可淘汰帧的数量减少一个。
  --curr_size_;

  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);

  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) < replacer_size_, "Invalid frame id");

  auto &frame = frames_[frame_id];
  frame.history_.push_back(current_timestamp_++);

  if (frame.history_.size() > k_) {
    frame.history_.pop_front();
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock<std::mutex> lock(latch_);

  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) < replacer_size_, "Invalid frame id");

  auto it = frames_.find(frame_id);
  if (it == frames_.end()) {
    return;
  }

  auto &frame = it->second;
  if (frame.evictable_ == set_evictable) {
    return;
  }

  frame.evictable_ = set_evictable;
  if (set_evictable) {
    ++curr_size_;
  } else {
    --curr_size_;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock<std::mutex> lock(latch_);

  auto it = frames_.find(frame_id);
  if (it == frames_.end()) {
    return;
  }

  BUSTUB_ASSERT(it->second.evictable_, "Cannot remove a non-evictable frame");

  frames_.erase(it);
  --curr_size_;
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace bustub
