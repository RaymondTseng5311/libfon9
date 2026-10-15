// \file f9set/ExgMdSystem.cpp
#include "f9set/ExgMdSystem.hpp"
#include "f9set/ExgMdReceiverFactory.hpp"
#include "fon9/framework/IoManagerTree.hpp"
#include "fon9/seed/SysEnv.hpp"
#include "fon9/Log.hpp"

namespace f9set {

class ExgMdSystem::Builder : public ExgMdRtsBuilder {
   fon9_NON_COPY_NON_MOVE(Builder);
   using base = ExgMdRtsBuilder;
   ExgMdSystem& Owner_;
public:
   Builder(ExgMdSystem& owner) : base{owner.Symbs_}, Owner_(owner) {
      // 缺號時先等另一路(A/B) 補上, 再透過 Rr 要求回補; 沒有 Rr 則放棄缺號.
      this->SetRecoverEnabled(true);
   }
protected:
   void OnMoldGap(uint64_t expectedSeqNo, uint64_t receivedSeqNo) override {
      fon9_LOG_INFO(this->Owner_.Name_, ".MoldGap|from=", expectedSeqNo, "|to=", receivedSeqNo,
                    "|pending=", this->GetPendingCount());
      this->Owner_.RecoverTimer_.RunAfter(this->RecoverArgs_.GapWait_);
   }
   void OnMoldLost(uint64_t fromSeqNo, uint64_t toSeqNo) override {
      fon9_LOG_WARN(this->Owner_.Name_, ".MoldLost|from=", fromSeqNo, "|to=", toSeqNo,
                    "|count=", toSeqNo - fromSeqNo, "|hasRr=", this->Owner_.RrDevice_ ? 'Y' : 'N');
      this->Owner_.CheckGlimpseRequest();
   }
   bool OnMoldSendRequest(const MoldUdp64Request& req) override {
      if (!this->Owner_.RrDevice_)
         return false;
      auto res = this->Owner_.RrDevice_->SendASAP(&req, sizeof(req));
      fon9_LOG_INFO(this->Owner_.Name_, ".MoldRequest|seq=", req.SeqNo_.Get(), "|count=", req.MsgCount_.Get(),
                    "|res=", res.IsError() ? fon9::StrView{"err"} : fon9::StrView{"ok"});
      return !res.IsError();
   }
   void OnMoldSessionChanged(const MoldUdp64Session& newSession) override {
      fon9_LOG_IMP(this->Owner_.Name_, ".MoldSession|old=", ToStrView(this->GetSession()),
                   "|new=", ToStrView(newSession));
   }
   void OnMoldEndOfSession() override {
      fon9_LOG_IMP(this->Owner_.Name_, ".MoldEndOfSession|session=", ToStrView(this->GetSession()),
                   "|nextSeq=", this->GetNextSeqNo());
   }
   void OnSnapshotDone() override {
      base::OnSnapshotDone();
      fon9_LOG_IMP(this->Owner_.Name_, ".SnapshotDone|nextSeq=", this->GetNextSeqNo(),
                   "|books=", this->GetOrderBookCount());
   }
};
//--------------------------------------------------------------------------//
ExgMdSystem::ExgMdSystem(fon9::seed::MaTreeSP root, std::string name, std::string rtiNamePre, bool isAddMarketSeq)
   : base(root, name)
   , Symbs_{new ExgMdSymbs(rtiNamePre.empty() ? std::string{} : (rtiNamePre + "Symbs"), isAddMarketSeq)} {
   this->Builder_.reset(new Builder{*this});
   this->Sapling_->AddNamedSapling(this->Symbs_, "Symbs");
   this->Symbs_->SetDailyClearHHMMSS(this->GetClearHHMMSS());
}
ExgMdSystem::ExgMdSystem(fon9::seed::MaTreeSP root, std::string name, bool useRtiForRecover, bool isAddMarketSeq)
   : ExgMdSystem(root, name,
                 useRtiForRecover ? (fon9::seed::SysEnv_GetLogFileFmtPath(*root) + name + "_") : std::string{},
                 isAddMarketSeq) {
}
ExgMdSystem::~ExgMdSystem() {
   this->RecoverTimer_.DisposeAndWait();
   this->HbTimer_.DisposeAndWait();
}
ExgMdRtsBuilder& ExgMdSystem::GetBuilder() {
   return *this->Builder_;
}
fon9::IoManagerSP ExgMdSystem::PlantIoMgr(fon9::seed::MaTree& root, fon9::IoManagerArgs& iomArgs) {
   if (!iomArgs.DeviceFactoryPark_)
      iomArgs.DeviceFactoryPark_ = fon9::seed::FetchNamedPark<fon9::DeviceFactoryPark>(root, "FpDevice");
   iomArgs.SessionFactoryPark_ = fon9::seed::FetchNamedPark<fon9::SessionFactoryPark>(*this->Sapling_, "FpSession");
   iomArgs.SessionFactoryPark_->Add(new ExgMdReceiverFactory(this, fon9::Named{"SetMdRecv"}));
   if (iomArgs.Name_.empty())
      iomArgs.Name_ = this->Name_ + "_io";
   return fon9::IoManagerTree::Plant(*this->Sapling_, iomArgs);
}
void ExgMdSystem::SetTimeZoneOffset(fon9::TimeInterval ti) {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   this->Builder_->TimeZoneOffset_ = ti;
}
void ExgMdSystem::SetRecoverArgs(const MoldUdp64RecoverArgs& args) {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   this->Builder_->RecoverArgs_ = args;
}
//--------------------------------------------------------------------------//
void ExgMdSystem::OnParentTreeClear(fon9::seed::Tree& parent) {
   this->Symbs_->SaveTo(this->LogPathPre() + "_Symbs.mds");
   base::OnParentTreeClear(parent);
   this->RecoverTimer_.DisposeAndWait();
   std::lock_guard<std::mutex> lk{this->Mutex_};
   this->RrDevice_.reset();
   this->GlimpseDevice_.reset();
}
void ExgMdSystem::OnMdClearTimeChanged() {
   this->Symbs_->SetDailyClearHHMMSS(this->GetClearHHMMSS());
}
void ExgMdSystem::OnMdSystemStartup(const unsigned tdayYYYYMMDD, const std::string& logPath) {
   base::OnMdSystemStartup(tdayYYYYMMDD, logPath);
   this->Symbs_->DailyClear(tdayYYYYMMDD);
   this->Symbs_->LoadFrom(this->LogPathPre() + "_Symbs.mds");
}
fon9::TimeInterval ExgMdSystem::OnMdSystem_HbTimer(fon9::TimeStamp now) {
   (void)now;
   uint64_t pkCount;
   {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      pkCount = this->Builder_->PkCount_;
   }
   if (pkCount == this->LastHbPkCount_)
      fon9_LOG_WARN(this->Name_, ".Hb|interval=", kMdHbInterval, "|info=No data");
   this->LastHbPkCount_ = pkCount;
   return kMdHbInterval;
}
//--------------------------------------------------------------------------//
void ExgMdSystem::EmitOnRecoverTimer(fon9::TimerEntry* timer, fon9::TimeStamp now) {
   ExgMdSystem&       rthis = fon9::ContainerOf(*static_cast<RecoverTimer*>(timer), &ExgMdSystem::RecoverTimer_);
   fon9::TimeInterval ti;
   {
      std::lock_guard<std::mutex> lk{rthis.Mutex_};
      ti = rthis.Builder_->CheckRecover(now);
      rthis.Builder_->FlushBS();
   }
   if (ti.GetOrigValue() > 0)
      timer->RunAfter(ti);
}
MoldUdp64Result ExgMdSystem::OnMoldPacket(const void* pk, size_t pksz) {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   const MoldUdp64Result res = this->Builder_->FeedPacket(pk, pksz);
   this->Builder_->FlushBS();
   return res;
}
void ExgMdSystem::SetRrDevice(fon9::io::Device* dev, fon9::io::Device* cur) {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   if (dev || this->RrDevice_.get() == cur)
      this->RrDevice_.reset(dev);
}
void ExgMdSystem::SetGlimpseDevice(fon9::io::Device* dev, fon9::io::Device* cur) {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   if (dev || this->GlimpseDevice_.get() == cur)
      this->GlimpseDevice_.reset(dev);
}
void ExgMdSystem::CheckGlimpseRequest() {
   // 在 this->Mutex_ 鎖定狀態下呼叫.
   if (!this->IsSnapshotOnLost_ || !this->GlimpseDevice_ || this->IsGlimpseRequested_
       || this->Builder_->IsWaitingSnapshot())
      return;
   this->IsGlimpseRequested_ = true;
   fon9_LOG_WARN(this->Name_, ".GlimpseRequest|info=Reopen GLIMPSE for snapshot");
   this->GlimpseDevice_->AsyncOpen(std::string{});
}
void ExgMdSystem::GlimpseBegin() {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   this->IsGlimpseRequested_ = false;
   this->Builder_->BeginSnapshot();
   fon9_LOG_IMP(this->Name_, ".GlimpseBegin|nextSeq=", this->Builder_->GetNextSeqNo());
}
bool ExgMdSystem::GlimpseMessage(uint64_t seqNo, const void* msg, unsigned msgsz) {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   if (!this->Builder_->IsWaitingSnapshot())
      return true;
   this->Builder_->ParseItchMessage(seqNo, msg, msgsz);
   if (this->Builder_->IsWaitingSnapshot())
      return false;
   this->Builder_->FlushBS();
   return true;
}
void ExgMdSystem::GlimpseAbort() {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   if (!this->Builder_->IsWaitingSnapshot())
      return;
   fon9_LOG_WARN(this->Name_, ".GlimpseAbort|pending=", this->Builder_->GetPendingCount());
   this->Builder_->AbortSnapshot();
   this->Builder_->FlushBS();
}
bool ExgMdSystem::IsWaitingSnapshot() {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   return this->Builder_->IsWaitingSnapshot();
}
std::string ExgMdSystem::GetInfo() {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   const ExgMdRtsBuilder& b = *this->Builder_;
   return fon9::RevPrintTo<std::string>(
      "session=", ToStrView(b.GetSession()),
      "|nextSeq=", b.GetNextSeqNo(),
      "|pk=", b.PkCount_, "|badPk=", b.BadPkCount_,
      "|msg=", b.MsgCount_, "|dup=", b.DupMsgCount_,
      "|gap=", b.GapCount_, '/', b.GapMsgCount_,
      "|req=", b.RequestCount_, "|pending=", b.GetPendingCount(),
      "|snapshot=", b.IsWaitingSnapshot() ? 'Y' : 'N',
      "|books=", b.GetOrderBookCount(),
      "|orderNotFound=", b.OrderNotFoundCount_,
      "|pubBS=", b.PubBSCount_, "|pubDeal=", b.PubDealCount_, "|pubSt=", b.PubSessionStCount_);
}

} // namespaces
