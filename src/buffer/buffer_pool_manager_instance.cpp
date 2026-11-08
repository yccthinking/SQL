//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_instance.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager_instance.h"

#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

BufferPoolManagerInstance::BufferPoolManagerInstance(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  page_table_ = new ExtendibleHashTable<page_id_t, frame_id_t>(bucket_size_);
  replacer_ = new LRUKReplacer(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManagerInstance::~BufferPoolManagerInstance() {
  delete[] pages_;
  delete page_table_;
  delete replacer_;
}

auto BufferPoolManagerInstance::NewPgImp(page_id_t *page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);

  // 如果后面无法创建页面，输出编号保持为无效值。
  *page_id = INVALID_PAGE_ID;

  frame_id_t frame_id;

  // 优先使用尚未存放页面的空闲帧。
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    // 没有空闲帧，请替换器选择一个可淘汰的帧。
    // Evict 成功时，也会清除这个帧原来的访问历史。
    if (!replacer_->Evict(&frame_id)) {
      // 所有帧都在使用中，无法创建新页面。
      return nullptr;
    }
  }

  auto &page = pages_[frame_id];

  // 如果这个帧原来存放着有效页面，先处理旧页面。
  if (page.page_id_ != INVALID_PAGE_ID) {
    // 脏页必须先写回磁盘，才能复用它的内存。
    if (page.is_dirty_) {
      disk_manager_->WritePage(page.page_id_, page.GetData());
    }

    // 旧页面不再位于缓冲池中，删除它的映射。
    page_table_->Remove(page.page_id_);
  }

  // 确定有可用帧后，才分配新页面编号。
  *page_id = AllocatePage();

  // 清空旧数据，并设置新页面的元信息。
  page.ResetMemory();
  page.page_id_ = *page_id;
  page.pin_count_ = 1;     // 创建者正在使用这个页面。
  page.is_dirty_ = false;  // 新页面暂未被调用者修改。

  // 记录新页面存放在哪个帧中。
  page_table_->Insert(*page_id, frame_id);

  // 记录一次访问，并禁止淘汰正在使用的帧。
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  // 返回缓冲池中这个页面对象的地址。
  return &page;
}

auto BufferPoolManagerInstance::FetchPgImp(page_id_t page_id) -> Page * {
  std::scoped_lock<std::mutex> lock(latch_);

  // 无效编号不能读取。
  if (page_id == INVALID_PAGE_ID) {
    return nullptr;
  }

  frame_id_t frame_id;

  // 情况一：页面已经在缓冲池中。
  if (page_table_->Find(page_id, frame_id)) {
    auto &page = pages_[frame_id];

    // 当前调用者开始使用这个页面，增加引用计数。
    ++page.pin_count_;

    // 记录访问，并禁止淘汰。
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);

    return &page;
  }

  // 情况二：页面不在缓冲池中，需要从磁盘读入。
  // 优先取一个空闲帧。
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    // 没有空闲帧，就尝试淘汰一个旧页面。
    if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    }
  }

  auto &page = pages_[frame_id];

  // 如果帧中有旧页面，先处理旧页面。
  if (page.page_id_ != INVALID_PAGE_ID) {
    // 旧页面被修改过，必须先保存到磁盘。
    if (page.is_dirty_) {
      disk_manager_->WritePage(page.page_id_, page.GetData());
    }

    // 删除旧页面在缓冲池中的位置记录。
    page_table_->Remove(page.page_id_);
  }

  // 清空帧中的旧数据，再读入目标页面的数据。
  page.ResetMemory();
  disk_manager_->ReadPage(page_id, page.GetData());

  // 设置目标页面的元信息。
  page.page_id_ = page_id;
  page.pin_count_ = 1;
  page.is_dirty_ = false;

  // 记录目标页面的位置。
  page_table_->Insert(page_id, frame_id);

  // 记录访问，并禁止淘汰正在使用的帧。
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return &page;
}

auto BufferPoolManagerInstance::UnpinPgImp(page_id_t page_id, bool is_dirty) -> bool {
  // 保护缓冲池的页面状态及相关管理操作。
  std::scoped_lock<std::mutex> lock(latch_);

  // 查找页面所在的内存帧；页面不在缓冲池中时返回失败。
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }

  // 取得这个帧中的页面对象，使用引用以修改原对象。
  auto &page = pages_[frame_id];

  // 引用计数已经为 0，不能再次减少。
  if (page.pin_count_ <= 0) {
    return false;
  }

  // 当前调用者使用完毕，减少一次引用。
  --page.pin_count_;

  // 只要之前或本次有修改，页面就仍然是脏页。
  page.is_dirty_ = page.is_dirty_ || is_dirty;

  // 没有调用者继续使用时，允许替换器淘汰这个帧。
  if (page.pin_count_ == 0) {
    replacer_->SetEvictable(frame_id, true);
  }

  return true;
}

auto BufferPoolManagerInstance::FlushPgImp(page_id_t page_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);

  // 无效页面编号不能刷新。
  if (page_id == INVALID_PAGE_ID) {
    return false;
  }

  // 查找页面所在的帧；不在缓冲池中则返回失败。
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    return false;
  }

  auto &page = pages_[frame_id];

  // 将页面的数据写入磁盘。
  // 即使页面不是脏页，也要执行写入。
  disk_manager_->WritePage(page_id, page.GetData());

  // 写回后，内存内容与磁盘一致，清除脏标记。
  page.is_dirty_ = false;

  return true;
}

void BufferPoolManagerInstance::FlushAllPgsImp() {
  std::scoped_lock<std::mutex> lock(latch_);

  for (size_t i = 0; i < pool_size_; ++i) {
    auto &page = pages_[i];

    // 空闲帧没有有效页面，跳过。
    if (page.page_id_ == INVALID_PAGE_ID) {
      continue;
    }

    // 写回有效页面，并清除脏标记。
    disk_manager_->WritePage(page.page_id_, page.GetData());
    page.is_dirty_ = false;
  }
}

auto BufferPoolManagerInstance::DeletePgImp(page_id_t page_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);

  // 页面不在缓冲池中，按接口要求直接返回成功。
  frame_id_t frame_id;
  if (!page_table_->Find(page_id, frame_id)) {
    return true;
  }

  auto &page = pages_[frame_id];

  // 还有调用者正在使用这个页面，不能删除。
  if (page.pin_count_ > 0) {
    return false;
  }

  // 删除“页面编号 → 帧编号”的映射。
  page_table_->Remove(page_id);

  // 清除替换器中该帧的访问历史，并减少可淘汰帧数。
  replacer_->Remove(frame_id);

  // 清空页面数据，并将帧恢复为未存放页面的状态。
  page.ResetMemory();
  page.page_id_ = INVALID_PAGE_ID;
  page.pin_count_ = 0;
  page.is_dirty_ = false;

  // 归还空闲帧，以后可用于存放其他页面。
  free_list_.push_back(frame_id);

  // 调用框架提供的页面释放接口。
  DeallocatePage(page_id);

  return true;
}

auto BufferPoolManagerInstance::AllocatePage() -> page_id_t { return next_page_id_++; }

}  // namespace bustub
