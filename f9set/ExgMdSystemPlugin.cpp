// \file f9set/ExgMdSystemPlugin.cpp
//
// Plugin: 建立 SET 行情系統(ExgMdSystem).
//
// 設定範例(EntryName=SetMdSys):
//    Name=SetMd|AddMarketSeq=Y|UseRtiForRecover=Y|TimeZone=7|SnapshotOnLost=Y|Io={SvcCfg='ThreadCount=1'|Cfg=SetMd_io.f9gv}
// - Name:              系統名稱, 預設 SetMd; 商品表為 "/SetMd/Symbs", 行情來源為 "/SetMd/SetMd_io";
// - AddMarketSeq:      是否在 Deal/BS 加上 MarketSeq(=MoldUDP64 序號), 預設 Y;
// - UseRtiForRecover:  是否將即時資料存檔, 提供訂閱者回補, 預設 Y;
// - TimeZone:          交易所時區, 例: 7, +7, 5:30, -3; 預設 7;
// - NoDataEventSch:    有資料的時段, 例: 'Start=06:00|End=18:00'; 也決定每日清檔時間(Start);
// - SnapshotOnLost:    放棄缺號後, 是否重新取得 GLIMPSE 快照, 預設 Y;
// - GapWait, RetryInterval, MaxRetry: 缺號回補參數, 請參考 f9set::MoldUdp64RecoverArgs;
// - Io:                IoManager 參數, 請參考 fon9::IoManagerArgs::SetTagValue();
//                      若沒提供 Cfg, 則使用 {Name}_io.f9gv;
// 行情來源的 Session 設定, 請參考 f9set/ExgMdReceiverFactory.hpp.
//
#include "f9set/ExgMdSystem.hpp"
#include "fon9/seed/Plugins.hpp"
#include "fon9/seed/SysEnv.hpp"
#include "fon9/StrTo.hpp"

namespace f9set {

static bool SetMdSys_Start(fon9::seed::PluginsHolder& holder, fon9::StrView args) {
   std::string          name{"SetMd"};
   bool                 isAddMarketSeq = true;
   bool                 useRti = true;
   bool                 isSnapshotOnLost = true;
   fon9::TimeInterval   tzOffset = fon9::TimeInterval_Hour(7);
   fon9::StrView        noDataEventSch, ioArgs;
   MoldUdp64RecoverArgs recoverArgs;
   fon9::StrView        tag, value;
   while (fon9::SbrFetchTagValue(args, tag, value)) {
      if (tag == "Name")
         name = value.ToString();
      else if (tag == "AddMarketSeq")
         isAddMarketSeq = (fon9::toupper(value.Get1st()) == 'Y');
      else if (tag == "UseRtiForRecover")
         useRti = (fon9::toupper(value.Get1st()) == 'Y');
      else if (tag == "SnapshotOnLost")
         isSnapshotOnLost = (fon9::toupper(value.Get1st()) == 'Y');
      else if (tag == "TimeZone") {
         // "7" 或 "+7" 或 "5:30" 或 "-3".
         const bool isNeg = (value.Get1st() == '-');
         if (isNeg || value.Get1st() == '+')
            value.SetBegin(value.begin() + 1);
         const char* pend;
         int         mins = fon9::StrTo(value, 0, &pend) * 60;
         if (pend < value.end() && *pend == ':')
            mins += fon9::StrTo(fon9::StrView{pend + 1, value.end()}, 0);
         tzOffset = fon9::TimeInterval_Minute(isNeg ? -mins : mins);
      }
      else if (tag == "NoDataEventSch")
         noDataEventSch = value;
      else if (tag == "GapWait")
         recoverArgs.GapWait_ = fon9::StrTo(value, recoverArgs.GapWait_);
      else if (tag == "RetryInterval")
         recoverArgs.RetryInterval_ = fon9::StrTo(value, recoverArgs.RetryInterval_);
      else if (tag == "MaxRetry")
         recoverArgs.MaxRetry_ = fon9::StrTo(value, recoverArgs.MaxRetry_);
      else if (tag == "Io")
         ioArgs = value;
      else {
         holder.SetPluginsSt(fon9::LogLevel::Error, "SetMdSys|err=Unknown tag: ", tag);
         return false;
      }
   }
   fon9::IoManagerArgs iomArgs;
   iomArgs.Name_ = name + "_io";
   iomArgs.IoServiceCfgstr_ = "ThreadCount=1";
   if (!(value = fon9::SbrFetchInsideNoTrim(ioArgs)).IsNull())
      ioArgs = value;
   while (fon9::SbrFetchTagValue(ioArgs, tag, value)) {
      if (!iomArgs.SetTagValue(holder, tag, value)) {
         holder.SetPluginsSt(fon9::LogLevel::Error, "SetMdSys|err=Unknown Io tag: ", tag);
         return false;
      }
   }
   if (iomArgs.CfgFileName_.empty())
      iomArgs.CfgFileName_ = fon9::seed::SysEnv_GetConfigPath(*holder.Root_).ToString() + iomArgs.Name_ + ".f9gv";
   ExgMdSystemSP mdsys{new ExgMdSystem(holder.Root_, name, useRti, isAddMarketSeq)};
   mdsys->SetTimeZoneOffset(tzOffset);
   mdsys->SetRecoverArgs(recoverArgs);
   mdsys->IsSnapshotOnLost_ = isSnapshotOnLost;
   if (!noDataEventSch.empty())
      mdsys->SetNoDataEventSch(noDataEventSch);

   if (!holder.Root_->Add(mdsys)) {
      holder.SetPluginsSt(fon9::LogLevel::Error, "SetMdSys|err=Dup name: ", name);
      return false;
   }
   mdsys->PlantIoMgr(*holder.Root_, iomArgs);
   mdsys->StartupMdSystem();
   holder.SetPluginsSt(fon9::LogLevel::Info, "SetMdSys|name=", name, "|io=", iomArgs.Result_);
   return true;
}

} // namespaces

extern "C" f9set_API fon9::seed::PluginsDesc f9p_SetMdSys;
static fon9::seed::PluginsPark f9pAutoPluginsReg{"SetMdSys", &f9p_SetMdSys};
fon9::seed::PluginsDesc f9p_SetMdSys{"", &f9set::SetMdSys_Start, nullptr, nullptr,};
