﻿// \file fon9/PolicyTree.cpp
// \author fonwinz@gmail.com
#include "fon9/auth/PolicyTree.hpp"
#include "fon9/seed/FieldMaker.hpp"
#include "fon9/seed/TreeLockContainerT.hpp"

namespace fon9 { namespace auth {

PolicyTree::PolicyTree(std::string tabName, std::string keyName, seed::Fields&& fields)
   : base{new seed::Layout1(seed::FieldSP{new seed::FieldCharVector(seed::Named{std::move(keyName)}, 0)},
                            new seed::Tab{seed::Named{std::move(tabName)}, std::move(fields)})} {
}
void PolicyTree::OnParentSeedClear() {
   ItemMap  temp;
   {
      Maps::Locker maps{this->Maps_};
      maps->DeletedMap_.clear();
      maps->ItemMap_.swap(temp);
   }
   for (auto& seed : temp)
      seed.second->OnParentTreeClear(*this);
}

fon9_WARN_DISABLE_PADDING;
fon9_MSC_WARN_DISABLE_NO_PUSH(4265 /* class has virtual functions, but destructor is not virtual. */
                              4355 /* 'this' : used in base member initializer list*/);
struct PolicyTree::PodOp : public seed::PodOpLocker<PodOp, Maps::Locker> {
   fon9_NON_COPY_NON_MOVE(PodOp);
   using base = seed::PodOpLocker<PodOp, Maps::Locker>;
   PolicyItem& Seed_;
   bool        IsModified_{false};
   PodOp(PolicyItem& seed, Tree& sender, seed::OpResult res, Maps::Locker& locker, bool isForceWrite)
      : base{*this, sender, res, ToStrView(seed.PolicyId_), locker}
      , Seed_(seed)
      , IsModified_{isForceWrite} {
   }
   PolicyItem& GetSeedRW(seed::Tab&) {
      return this->Seed_;
   }
   void BeginWrite(seed::Tab& tab, seed::FnWriteOp fnCallback) override {
      base::BeginWrite(tab, std::move(fnCallback));
      this->IsModified_ = true;
   }

   seed::TreeSP HandleGetSapling(seed::Tab&) {
      return this->Seed_.GetSapling();
   }
   void HandleSeedCommand(SeedOpResult& res, StrView cmd, seed::FnCommandResultHandler&& resHandler) {
      this->Seed_.OnSeedCommand(res, cmd, std::move(resHandler));
   }
};

struct PolicyTree::TreeOp : public seed::TreeOp {
   fon9_NON_COPY_NON_MOVE(TreeOp);
   using base = seed::TreeOp;
   TreeOp(PolicyTree& tree) : base(tree) {
   }

   static void MakePolicyRecordView(ItemMap::iterator ivalue, seed::Tab* tab, RevBuffer& rbuf) {
      if (tab)
         FieldsCellRevPrint(tab->Fields_, seed::SimpleRawRd{*ivalue->second}, rbuf, seed::GridViewResult::kCellSplitter);
      RevPrint(rbuf, ivalue->first);
   }
   static ItemMap::iterator GetStartIterator(ItemMap& items, StrView strKeyText) {
      return base::GetStartIterator(items, strKeyText, [](const StrView& strKey) { return strKey; });
   }
   virtual void GridView(const seed::GridViewRequest& req, seed::FnGridViewOp fnCallback) {
      seed::GridViewResult res{this->Tree_};
      {
         Maps::Locker maps{static_cast<PolicyTree*>(&this->Tree_)->Maps_};
         seed::MakeGridView(maps->ItemMap_, this->GetStartIterator(maps->ItemMap_, req.OrigKey_),
                            req, res, &MakePolicyRecordView);
      } // unlock.
      fnCallback(res);
   }

   void OnPodOp(Maps::Locker& maps, PolicyItem& rec, seed::FnPodOp&& fnCallback, bool isForceWrite = false) {
      PodOp op{rec, this->Tree_, seed::OpResult::no_error, maps, isForceWrite};
      fnCallback(op, &op);
      if (op.IsModified_) {
         op.Lock();
         maps->WriteUpdated(rec);
      }
   }
   virtual void Get(StrView strKeyText, seed::FnPodOp fnCallback) override {
      {
         Maps::Locker maps{static_cast<PolicyTree*>(&this->Tree_)->Maps_};
         auto         ifind = this->GetFindIterator(maps->ItemMap_, strKeyText, [](const StrView& strKey) { return strKey; });
         if (ifind != maps->ItemMap_.end()) {
            this->OnPodOp(maps, *ifind->second, std::move(fnCallback));
            return;
         }
      } // unlock.
      fnCallback(seed::PodOpResult{this->Tree_, seed::OpResult::not_found_key, strKeyText}, nullptr);
   }
   virtual void Add(StrView strKeyText, seed::FnPodOp fnCallback) override {
      if (this->IsTextBegin(strKeyText) || this->IsTextEnd(strKeyText)) {
         fnCallback(seed::PodOpResult{this->Tree_, seed::OpResult::not_found_key, strKeyText}, nullptr);
         return;
      }
      Maps::Locker maps{static_cast<PolicyTree*>(&this->Tree_)->Maps_};
      auto         ifind = maps->ItemMap_.find(strKeyText);
      bool         isForceWrite = false;
      if (ifind == maps->ItemMap_.end()) {
         isForceWrite = true;
         PolicyItemSP rec = static_cast<PolicyTree*>(&this->Tree_)->MakePolicy(strKeyText);
         ifind = maps->ItemMap_.insert(ItemMap::value_type{ToStrView(rec->PolicyId_), std::move(rec)}).first;
      }
      this->OnPodOp(maps, *ifind->second, std::move(fnCallback), isForceWrite);
   }
   virtual void Remove(StrView strKeyText, seed::Tab* tab, seed::FnPodRemoved fnCallback) override {
      seed::PodRemoveResult res{this->Tree_, seed::OpResult::not_found_key, strKeyText, tab};
      if (static_cast<PolicyTree*>(&this->Tree_)->Delete(strKeyText))
         res.OpResult_ = seed::OpResult::removed_pod;
      fnCallback(res);
   }
};
fon9_WARN_POP;

void PolicyTree::OnTreeOp(seed::FnTreeOp fnCallback) {
   if (fnCallback) {
      TreeOp op{*this};
      fnCallback(seed::TreeOpResult{this, seed::OpResult::no_error}, &op);
   }
}

} } // namespaces
