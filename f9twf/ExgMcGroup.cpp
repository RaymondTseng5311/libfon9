﻿// \file f9twf/ExgMcGroup.cpp
// \author fonwinz@gmail.com
#include "f9twf/ExgMcGroup.hpp"
#include "fon9/seed/SysEnv.hpp"

namespace f9twf {

ExgMcSystem::ExgMcSystem(fon9::seed::MaTreeSP root, std::string name)
   : base{name}
   , Root_{std::move(root)}
   , Symbs_{new ExgMdSymbs} {
   this->Sapling_->AddNamedSapling(this->Symbs_, "Symbs");
}
ExgMcSystem::~ExgMcSystem() {
   this->ClearTimer_.DisposeAndWait();
}
void ExgMcSystem::OnParentTreeClear(fon9::seed::Tree& parent) {
   base::OnParentTreeClear(parent);
   this->ClearTimer_.DisposeAndWait();
}
void ExgMcSystem::EmitOnClearTimer(fon9::TimerEntry* timer, fon9::TimeStamp now) {
   (void)now;
   ExgMcSystem& rthis = ContainerOf(*static_cast<ClearTimer*>(timer), &ExgMcSystem::ClearTimer_);
   rthis.StartupMcSystem();
}
void ExgMcSystem::SetClearHHMMSS(unsigned clearHHMMSS) {
   if (this->ClearHHMMSS_ == clearHHMMSS)
      return;
   this->ClearHHMMSS_ = clearHHMMSS;
   if (this->TDayYYYYMMDD_) // 必須有 StartupMcSystem() 啟動過, 才需要重啟計時器.
      this->StartupMcSystem();
}
unsigned ExgMcSystem::CheckTDayYYYYMMDD(fon9::TimeStamp tm) const {
   if (fon9::GetHHMMSS(tm) < this->ClearHHMMSS_)
      tm -= fon9::TimeInterval_Day(1);
   return fon9::GetYYYYMMDD(tm);
}
bool ExgMcSystem::Startup(const unsigned tdayYYYYMMDD) {
   if (tdayYYYYMMDD == this->TDayYYYYMMDD_)
      return false;
   fon9_LOG_IMP("ExgMcSystem.Startup|name=", this->Name_, "|tday=", tdayYYYYMMDD);
   this->TDayYYYYMMDD_ = tdayYYYYMMDD;
   fon9::TimedFileName logfn(fon9::seed::SysEnv_GetLogFileFmtPath(*this->Root_), fon9::TimedFileName::TimeScale::Day);
   // 檔名與 TDay 相關, 與 TimeZone 無關, 所以要扣除 logfn.GetTimeChecker().GetTimeZoneOffset();
   logfn.RebuildFileName(fon9::YYYYMMDDHHMMSS_ToTimeStamp(tdayYYYYMMDD, 0) - logfn.GetTimeChecker().GetTimeZoneOffset());
   // logPath = "logs/yyyymmdd/"
   const std::string logPath = logfn.GetFileName();

   this->Symbs_->DailyClear();
   auto seeds = this->Sapling_->GetList(nullptr);
   for (fon9::seed::NamedSeedSP& seed : seeds) {
      if (auto* mcGroup = dynamic_cast<ExgMcGroup*>(seed.get()))
         mcGroup->StartupMcGroup(*this, logPath);
   }
   return true;
}
void ExgMcSystem::StartupMcSystem() {
   this->Startup(this->CheckTDayYYYYMMDD(fon9::LocalNow()));
   fon9::TimeStamp tm = fon9::YYYYMMDDHHMMSS_ToTimeStamp(this->TDayYYYYMMDD_, this->ClearHHMMSS_)
                      + fon9::TimeInterval_Day(1);
   fon9_LOG_IMP("ExgMcSystem.NextClear|name=", this->Name_, "|time=", tm);
   this->ClearTimer_.RunAt(tm - fon9::GetLocalTimeZoneOffset());
}
//--------------------------------------------------------------------------//
ExgMcGroup::ExgMcGroup(ExgMcSystem* mdsys, std::string name)
   : base(std::move(name))
   , ChannelMgr_{new ExgMcChannelMgr(mdsys->Symbs_, &mdsys->Name_, &Name_)} {
}
ExgMcGroup::~ExgMcGroup() {
}
void ExgMcGroup::StartupMcGroup(ExgMcSystem& mdsys, std::string logPath) {
   (void)mdsys;
   // logPath = "logs/yyyymmdd/TwfMd_MdDay_"
   logPath += this->ChannelMgr_->Name_ + "_";
   // 在設定 Channel 時, PkLog 的檔名為 sysLogPath + "NNNN.bin"; 其中 NNNN = ChannelId;
   this->ChannelMgr_->StartupChannelMgr(logPath);

   // IoMgr 必須在 StartupChannelMgr() 之後才啟動. 避免 Channel 還沒準備好, 但 IoMgr 已收到封包.
   auto seeds = this->Sapling_->GetList(nullptr);
   for (fon9::seed::NamedSeedSP& seed : seeds) {
      auto* handler = dynamic_cast<ExgMcGroupSetupHandler*>(seed.get());
      if (handler)
         handler->OnStartupMcGroup(*this, logPath);
      if ((handler = dynamic_cast<ExgMcGroupSetupHandler*>(seed->GetSapling().get())) != nullptr)
         handler->OnStartupMcGroup(*this, logPath);
   }
}
//--------------------------------------------------------------------------//
ExgMcGroupSetupHandler::~ExgMcGroupSetupHandler() {
}
ExgMcGroupIoMgr::~ExgMcGroupIoMgr() {
}
void ExgMcGroupIoMgr::OnStartupMcGroup(ExgMcGroup&, const std::string& logPath) {
   (void)logPath;
   this->DisposeAndReopen("StartupMcGroup", fon9::TimeInterval_Second(1));
}

} // namespaces
