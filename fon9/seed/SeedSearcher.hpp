﻿/// \file fon9/seed/SeedSearcher.hpp
/// \author fonwinz@gmail.com
#ifndef __fon9_seed_SeedSearcher_hpp__
#define __fon9_seed_SeedSearcher_hpp__
#include "fon9/seed/PodOp.hpp"
#include "fon9/CharVector.hpp"

namespace fon9 { namespace seed {
fon9_WARN_DISABLE_PADDING;

struct SeedSearcher;
using SeedSearcherSP = intrusive_ptr<SeedSearcher>;

/// \ingroup seed
/// 解析 tabName = "keyText^tabName" 字串.
/// - 解析完畢後, 返回 keyText, 剩餘 tabName.
/// - keyText 可用引號, 返回時會移除引號.
/// - 若 keyText 沒有引號, 且為空白, 則使用 TreeOp::TextBegin()
fon9_API StrView ParseKeyTextAndTabName(StrView& tabName);

/// \ingroup seed
/// - 此處所有的 virtual function 都可能在任意 thread 被呼叫.
/// - 此為一次性的物件, 使用完畢後會自動刪除.
struct fon9_API SeedSearcher : public intrusive_ref_counter<SeedSearcher> {
   fon9_NON_COPY_NON_MOVE(SeedSearcher);
   const CharVector  OrigPath_;
   StrView           RemainPath_;
   SeedSearcher(const StrView& path) : OrigPath_{path}, RemainPath_{ToStrView(OrigPath_)} {
   }
   /// 如果 Tree::OnTreeOp() 是非同步,
   /// 則可能在尚未執行前就死亡, 衍生者須注意.
   virtual ~SeedSearcher();

   /// 可以從 this->RemainPath_.begin() 得知在哪個點遇到了問題.
   virtual void OnError(OpResult res) = 0;

   /// 在 Tree::OnTreeOp() 回呼時,
   /// 若 this->RemainPath_.empty() 則會呼叫此處.
   /// 通常此時會執行 opTree.GridView(), 或其他批次作業.
   virtual void OnFoundTree(TreeOp& opTree) = 0;

   /// 在取出最後一個 keyText, tab 之後, 呼叫此處執行最後的操作.
   /// - 此時 this->RemainPath_.empty()
   /// - 預設: this->ContinueSeed(opTree, keyText, tab); 然後觸發 OnFoundTree();
   /// - 可能的操作:
   ///   - opTree.Get() for [(Read) or (ContinueSearch for GridView)]
   ///   - opTree.Add() for [(Write)]
   ///   - opTree.Remove()
   virtual void OnLastStep(TreeOp& opTree, StrView keyText, Tab& tab);

   /// 解析 this->RemainPath: "keyText^tabName/Remain" 並移除 Remain 前方的 '/' 之後.
   /// 預設: 取得 tab 後, 呼叫 OnLastStep() 或 ContinueSeed() 或 OnError();
   virtual void ContinuePod(TreeOp& opTree, StrView keyText, StrView tabName);

   struct PodHandler {
      SeedSearcherSP Searcher_;
      const char*    KeyPos_;
      Tab*           Tab_;
      void operator()(const PodOpResult& resPod, PodOp* opPod);
   };
   /// 預設: opTree.Get(keyText, PodHandler{this, keyText.begin(), &tab});
   /// 若有必要您可以 override 改成 opTree.Add(keyText, PodHandler{this, keyText.begin(), &tab});
   virtual void ContinueSeed(TreeOp& opTree, StrView keyText, Tab& tab);

   /// 預設使用 opPod.GetSapling();
   /// 若有必要您可以 override 改成 opPod.MakeSapling();
   virtual TreeSP ContinueTree(PodOp& opPod, Tab& tab);

protected:
   void ContinuePodForRemove(TreeOp& opTree, StrView keyText, StrView tabName, FnPodRemoved& removedHandler);
   /// 在 ContinuePodForRemove() 裡面:
   /// 提供衍生者(例如 SeedVisitor) 在 opTree.Remove() 前後有 log 的機會, 預設 do nothing.
   virtual void OnBeforeRemove(TreeOp& opTree, StrView keyText, Tab* tab);
   virtual void OnAfterRemove(const PodRemoveResult& res);
};

fon9_API void StartSeedSearch(Tree& root, SeedSearcherSP searcher);

//--------------------------------------------------------------------------//

class fon9_API GridViewSearcher : public SeedSearcher {
   fon9_NON_COPY_NON_MOVE(GridViewSearcher);
   using base = SeedSearcher;
public:
   GridViewRequest Request_;
   FnGridViewOp    Handler_;
   CharVector      OrigKey_;
   CharVector      TabName_;
   int             TabIndex_{-1};

   GridViewSearcher(const StrView& path, const GridViewRequest& req, FnGridViewOp&& handler);
   GridViewSearcher(const StrView& path, const GridViewRequest& req, int tabIndex, FnGridViewOp&& handler)
      : GridViewSearcher{path, req, std::move(handler)} {
      this->TabIndex_ = tabIndex;
   }
   GridViewSearcher(const StrView& path, const GridViewRequest& req, const StrView& tabName, FnGridViewOp&& handler)
      : GridViewSearcher{path, req, std::move(handler)} {
      this->TabName_.assign(tabName);
   }

   void OnError(OpResult opRes) override;

   /// 取得特定 tab 的 GridView, 則應覆寫此函式, Tab 的選擇:
   /// - 若 this->TabIndex_ < 0 && this->TabName_.empty() 則僅取出 key view.
   /// - this->TabIndex_ >= 0 優先, 然後才使用 this->TabName_
   void OnFoundTree(TreeOp& opTree) override;
};

inline void GetGridView(Tree& root, const StrView& path, const GridViewRequest& req, FnGridViewOp fnCallback) {
   StartSeedSearch(root, new GridViewSearcher(path, req, std::move(fnCallback)));
}
inline void GetGridView(Tree& root, const StrView& path, const GridViewRequest& req, int tabIndex, FnGridViewOp fnCallback) {
   StartSeedSearch(root, new GridViewSearcher(path, req, tabIndex, std::move(fnCallback)));
}
inline void GetGridView(Tree& root, const StrView& path, const GridViewRequest& req, const StrView& tabName, FnGridViewOp fnCallback) {
   StartSeedSearch(root, new GridViewSearcher(path, req, tabName, std::move(fnCallback)));
}

//--------------------------------------------------------------------------//

class fon9_API RemoveSeedSearcher : public SeedSearcher {
   fon9_NON_COPY_NON_MOVE(RemoveSeedSearcher);
   using base = SeedSearcher;
public:
   FnPodRemoved   Handler_;
   RemoveSeedSearcher(const StrView& path, FnPodRemoved handler)
      : base(path)
      , Handler_{std::move(handler)} {
   }

   void OnError(OpResult opRes) override;
   /// cannot remove "/"
   void OnFoundTree(TreeOp& opTree) override;
   void ContinuePod(TreeOp& opTree, StrView keyText, StrView tabName) override;
};

inline void RemoveSeed(Tree& root, const StrView& path, FnPodRemoved handler) {
   StartSeedSearch(root, new RemoveSeedSearcher(path, std::move(handler)));
}

//--------------------------------------------------------------------------//

class fon9_API WriteSeedSearcher : public SeedSearcher {
   fon9_NON_COPY_NON_MOVE(WriteSeedSearcher);
   using base = SeedSearcher;
public:
   using base::base;

   // 預設: opTree.Get(keyText, PodHandler{this, keyText.begin(), &tab});
   // 若有必要您可以 override 改成 opTree.Add(keyText, PodHandler{this, keyText.begin(), &tab});
   //void ContinueSeed(TreeOp& opTree, StrView keyText, Tab& tab) override;
   // 預設使用 opPod.GetSapling();
   // 若有必要您可以 override 改成 opPod.MakeSapling();
   //TreeSP ContinueTree(PodOp& opPod, Tab& tab) override;

   /// cannot remove "/"
   void OnFoundTree(TreeOp& opTree) override;
   /// 預設使用 opTree.Add() 進入 this->OnBeginWrite();
   void OnLastStep(TreeOp& opTree, StrView keyText, Tab& tab) override;
   virtual void OnBeginWrite(const SeedOpResult& res, const RawWr* wr) = 0;
};

class fon9_API PutFieldSearcher : public WriteSeedSearcher {
   fon9_NON_COPY_NON_MOVE(PutFieldSearcher);
   using base = WriteSeedSearcher;
   const int         FieldIndex_{-1};
   const CharVector  FieldName_{};
   const CharVector  FieldValue_;
public:
   PutFieldSearcher(const StrView& path, const StrView& fieldName, const StrView& fieldValue)
      : base{path}
      , FieldName_{fieldName}
      , FieldValue_{fieldValue} {
   }
   PutFieldSearcher(const StrView& path, size_t fieldIndex, const StrView& fieldValue)
      : base{path}
      , FieldIndex_{static_cast<int>(fieldIndex)}
      , FieldValue_{fieldValue} {
   }
   void OnBeginWrite(const SeedOpResult& res, const RawWr* wr) override;
   /// 預設 do nothing.
   virtual void OnFieldValueChanged(const SeedOpResult& res, const RawWr& wr, const Field& fld);
};

fon9_WARN_POP;
} } // namespaces
#endif//__fon9_seed_SeedSearcher_hpp__
