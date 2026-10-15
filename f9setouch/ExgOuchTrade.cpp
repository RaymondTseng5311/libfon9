// \file f9setouch/ExgOuchTrade.cpp
//
// Plugin "SetOuchTrade" 設定範例:
//    Name=SetOuch|TokenPrefix=A|Io={SvcCfg='ThreadCount=1'|Cfg=SetOuch_io.f9gv}
// - Name:        系統名稱, 預設 SetOuch; 線路管理員為 "/SetOuch";
// - TokenPrefix: Order Token 前置字元(最多4碼), 預設空白;
//                同一個 OUCH 帳號若有多個系統同時下單, 必須使用不同的 TokenPrefix;
// - Io:          IoManager 參數, 請參考 fon9::IoManagerArgs::SetTagValue();
//                若沒提供 Cfg, 則使用 {Name}_io.f9gv;
// 線路設定: Session = "SetOuch", SessionArgs 請參考 ExgOuchLineArgs, Device = TcpClient;
//    例: Session=SetOuch|SessionArgs=User=U00001|Pass=xxxx|Fc=100|Device=TcpClient|DeviceArgs=10.1.1.1:12345|ClosedReopen=5
//
#include "f9setouch/ExgOuchTrade.hpp"
#include "f9setouch/OuchFmtStr.hpp"
#include "fon9/seed/Plugins.hpp"
#include "fon9/seed/SysEnv.hpp"
#include "fon9/Log.hpp"
#include "fon9/StrTo.hpp"

namespace f9setouch {

f9setouch_API ExgOuchRequestSP MakeOuchRequest(fon9::StrView cmdln, std::string& err) {
   const fon9::StrView cmdop = fon9::StrFetchTrim(cmdln, &fon9::isspace);
   f9fmkt_RxKind       rxKind;
   if (cmdop == "new")
      rxKind = f9fmkt_RxKind_RequestNew;
   else if (cmdop == "chg")
      rxKind = f9fmkt_RxKind_RequestChgQty;
   else if (cmdop == "del")
      rxKind = f9fmkt_RxKind_RequestDelete;
   else {
      err = "Unknown command: " + cmdop.ToString();
      return nullptr;
   }
   ExgOuchRequestSP req{new ExgOuchRequest{rxKind}};
   auto fetchPri = [&cmdln, &err](OuchPrice& pri) {
      const fon9::StrView str = fon9::StrFetchTrim(cmdln, &fon9::isspace);
      const char*         pend;
      const auto          dec = fon9::StrTo(str, fon9::Decimal<int64_t, 8>::Null(), &pend);
      if (str.empty() || pend != str.end() || dec.IsNull()) {
         err = "Invalid price: " + str.ToString();
         return false;
      }
      // 價格的小數位數超過 OUCH 的小數位數, 視為錯誤(例: 34.255).
      static_assert(kOuchPriceDecimals <= 8, "kOuchPriceDecimals must <= 8");
      int64_t divisor = 1;
      for (unsigned L = kOuchPriceDecimals; L < 8; ++L)
         divisor *= 10;
      if (dec.GetOrigValue() % divisor != 0 || dec.GetOrigValue() / divisor > INT32_MAX || dec.GetOrigValue() < 0) {
         err = "Invalid price decimals: " + str.ToString();
         return false;
      }
      pri = OuchPrice::Make<kOuchPriceDecimals>(static_cast<int32_t>(dec.GetOrigValue() / divisor));
      return true;
   };
   auto fetchQty = [&cmdln, &err](f9fmkt::Qty& qty) {
      const fon9::StrView str = fon9::StrFetchTrim(cmdln, &fon9::isspace);
      const char*         pend;
      qty = fon9::StrTo(str, f9fmkt::Qty{0}, &pend);
      if (str.empty() || pend != str.end()) {
         err = "Invalid qty: " + str.ToString();
         return false;
      }
      return true;
   };
   switch (rxKind) {
   case f9fmkt_RxKind_RequestNew:
   {
      fon9::StrView str = fon9::StrFetchTrim(cmdln, &fon9::isspace);
      const char*   pend;
      req->OrderBookId_ = fon9::StrTo(str, uint32_t{0}, &pend);
      if (req->OrderBookId_ == 0 || pend != str.end()) {
         err = "Invalid OrderBookId: " + str.ToString();
         return nullptr;
      }
      str = fon9::StrFetchTrim(cmdln, &fon9::isspace);
      if (str.size() != 1 || (str.Get1st() != 'B' && str.Get1st() != 'S' && str.Get1st() != 'T')) {
         err = "Invalid side: " + str.ToString();
         return nullptr;
      }
      req->Side_ = static_cast<OuchSide>(str.Get1st());
      if (!fetchQty(req->Qty_) || req->Qty_ == 0) {
         if (err.empty())
            err = "Invalid qty: 0";
         return nullptr;
      }
      if (!fetchPri(req->Pri_))
         return nullptr;
      if (req->Side_ == OuchSide::ShortSell)
         req->ShortSellQty_ = req->Qty_;
      break;
   }
   case f9fmkt_RxKind_RequestChgQty:
      req->ExistingToken_.AssignFrom(fon9::StrFetchTrim(cmdln, &fon9::isspace));
      if (req->ExistingToken_.empty1st()) {
         err = "Require ExistingToken";
         return nullptr;
      }
      if (!fetchQty(req->Qty_) || !fetchPri(req->Pri_))
         return nullptr;
      if (req->Qty_ == 0)
         req->ForceResetRxKind(f9fmkt_RxKind_RequestChgPri);
      break;
   default: // f9fmkt_RxKind_RequestDelete
      req->OrderToken_.AssignFrom(fon9::StrFetchTrim(cmdln, &fon9::isspace));
      if (req->OrderToken_.empty1st()) {
         err = "Require Token";
         return nullptr;
      }
      break;
   }
   // 選項: Tif=, Acc=, Token=, SsQty=
   fon9::StrView tag, value;
   while (!fon9::StrTrimHead(&cmdln).empty()) {
      fon9::StrView opt = fon9::StrFetchTrim(cmdln, &fon9::isspace);
      tag = fon9::StrFetchTrim(opt, '=');
      value = opt;
      if (tag == "Tif") {
         const unsigned tif = fon9::StrTo(value, 99u);
         if (tif != 0 && tif != 3 && tif != 4) {
            err = "Invalid Tif: " + value.ToString();
            return nullptr;
         }
         req->TimeInForce_ = static_cast<OuchTimeInForce>(tif);
      }
      else if (tag == "Acc")
         req->ClientAccount_.AssignFrom(value);
      else if (tag == "Token" && rxKind != f9fmkt_RxKind_RequestDelete)
         req->OrderToken_.AssignFrom(value);
      else if (tag == "SsQty")
         req->ShortSellQty_ = fon9::StrTo(value, f9fmkt::Qty{0});
      else {
         err = "Unknown option: " + tag.ToString();
         return nullptr;
      }
   }
   return req;
}

//--------------------------------------------------------------------------//

void ExgOuchRptLogger::LogRpt(uint64_t seqNo, const void* msg, unsigned msgsz) {
   fon9::RevBufferList rbuf{256};
   RevPrintOuchOutbound(rbuf, msg, msgsz);
   fon9::RevPrint(rbuf, "SetOuch.Rpt|seq=", seqNo, '|');
   fon9_LOG_INFO(fon9::BufferTo<std::string>(rbuf.MoveOut()));
}
void ExgOuchRptLogger::OnOuchAccepted(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderAccepted& rpt) {
   this->LogRpt(seqNo, &rpt, sizeof(rpt));
}
void ExgOuchRptLogger::OnOuchRejected(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderRejected& rpt) {
   this->LogRpt(seqNo, &rpt, sizeof(rpt));
}
void ExgOuchRptLogger::OnOuchReplaced(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderReplaced& rpt) {
   this->LogRpt(seqNo, &rpt, sizeof(rpt));
}
void ExgOuchRptLogger::OnOuchCancelled(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderCancelled& rpt) {
   this->LogRpt(seqNo, &rpt, sizeof(rpt));
}
void ExgOuchRptLogger::OnOuchExecuted(ExgOuchTradingLine&, uint64_t seqNo, const OuchOrderExecuted& rpt) {
   this->LogRpt(seqNo, &rpt, sizeof(rpt));
}
void ExgOuchRptLogger::OnOuchUnknown(ExgOuchTradingLine&, uint64_t seqNo, const void* msg, unsigned msgsz) {
   this->LogRpt(seqNo, msg, msgsz);
}
void ExgOuchRptLogger::OnOuchNoReadyLine(f9fmkt::TradingRequest& req, fon9::StrView cause) {
   fon9::RevBufferList rbuf{256};
   req.RevPrint(rbuf);
   fon9::RevPrint(rbuf, "SetOuch.NoReadyLine|cause=", cause, '|');
   fon9_LOG_WARN(fon9::BufferTo<std::string>(rbuf.MoveOut()));
}

//--------------------------------------------------------------------------//

ExgOuchTradeSeed::ExgOuchTradeSeed(const fon9::IoManagerArgs& ioargs)
   : base(new ExgOuchLineMgr(ioargs), ioargs.Name_) {
   this->SetTitle(ioargs.CfgFileName_);
   this->SetDescription(ioargs.Result_);
}
ExgOuchTradeSeed::~ExgOuchTradeSeed() {
}
std::string ExgOuchTradeSeed::SendRequest(ExgOuchRequest& req) {
   const f9fmkt::SendRequestResult res = this->GetLineMgr().SendRequest(req);
   fon9::StrView rmsg;
   switch (res) {
   case f9fmkt::SendRequestResult::Sent:           rmsg = "Sent";          break;
   case f9fmkt::SendRequestResult::NoReadyLine:    rmsg = "NoReadyLine";   break;
   case f9fmkt::SendRequestResult::RejectRequest:  rmsg = "RejectRequest"; break;
   case f9fmkt::SendRequestResult::Queuing:        rmsg = "Queuing";       break;
   case f9fmkt::SendRequestResult::RequestEnd:     rmsg = "RequestEnd";    break;
   default:                                        rmsg = "Unknown";       break;
   }
   // Queuing 時 Token 尚未產生, 送出時才會填入.
   return fon9::RevPrintTo<std::string>("token=", ToStrView(req.OrderToken_), "|result=", rmsg);
}
void ExgOuchTradeSeed::OnSeedCommand(fon9::seed::SeedOpResult& res, fon9::StrView cmdln, fon9::seed::FnCommandResultHandler resHandler,
                                     fon9::seed::MaTreeBase::Locker&& ulk, fon9::seed::SeedVisitor* visitor) {
   fon9::StrView cmd = cmdln;
   fon9::StrView cmdop = fon9::StrFetchTrim(cmd, &fon9::isspace);
   if (cmdop == "new" || cmdop == "chg" || cmdop == "del") {
      ulk.unlock();
      std::string err;
      if (auto req = MakeOuchRequest(cmdln, err)) {
         const std::string rmsg = this->SendRequest(*req);
         resHandler(res, &rmsg);
      }
      else {
         res.OpResult_ = fon9::seed::OpResult::bad_command_argument;
         resHandler(res, &err);
      }
      return;
   }
   if (cmdop == "?") {
      ulk.unlock();
      resHandler(res,
                 "new" fon9_kCSTR_CELLSPL "OrderBookId Side(B|S|T) Qty Price [Tif=0|3|4] [Acc=] [Token=] [SsQty=]" fon9_kCSTR_ROWSPL
                 "chg" fon9_kCSTR_CELLSPL "ExistingToken Qty(0=no change) Price(0=no change) [Token=]" fon9_kCSTR_ROWSPL
                 "del" fon9_kCSTR_CELLSPL "Token");
      return;
   }
   base::OnSeedCommand(res, cmdln, std::move(resHandler), std::move(ulk), visitor);
}

//--------------------------------------------------------------------------//

static bool SetOuchTrade_Start(fon9::seed::PluginsHolder& holder, fon9::StrView args) {
   std::string   name{"SetOuch"};
   fon9::StrView tokenPrefix, ioArgs, tag, value;
   while (fon9::SbrFetchTagValue(args, tag, value)) {
      if (tag == "Name")
         name = value.ToString();
      else if (tag == "TokenPrefix")
         tokenPrefix = value;
      else if (tag == "Io")
         ioArgs = value;
      else {
         holder.SetPluginsSt(fon9::LogLevel::Error, "SetOuchTrade|err=Unknown tag: ", tag);
         return false;
      }
   }
   if (tokenPrefix.size() > 4) {
      holder.SetPluginsSt(fon9::LogLevel::Error, "SetOuchTrade|err=TokenPrefix too long(max 4): ", tokenPrefix);
      return false;
   }
   auto devfp = fon9::seed::FetchNamedPark<fon9::DeviceFactoryPark>(*holder.Root_, "FpDevice");
   if (!devfp) {
      holder.SetPluginsSt(fon9::LogLevel::Error, "SetOuchTrade|err=DeviceFactoryPark not found: FpDevice");
      return false;
   }
   fon9::IoManagerArgs iomArgs;
   iomArgs.Name_ = name;
   iomArgs.IoServiceCfgstr_ = "ThreadCount=1";
   if (!(value = fon9::SbrFetchInsideNoTrim(ioArgs)).IsNull())
      ioArgs = value;
   while (fon9::SbrFetchTagValue(ioArgs, tag, value)) {
      if (!iomArgs.SetTagValue(holder, tag, value)) {
         holder.SetPluginsSt(fon9::LogLevel::Error, "SetOuchTrade|err=Unknown Io tag: ", tag);
         return false;
      }
   }
   iomArgs.Name_ = name; // 避免 Io={Name=...} 改變名稱.
   if (iomArgs.CfgFileName_.empty())
      iomArgs.CfgFileName_ = fon9::seed::SysEnv_GetConfigPath(*holder.Root_).ToString() + name + "_io.f9gv";
   if (!iomArgs.DeviceFactoryPark_)
      iomArgs.DeviceFactoryPark_ = devfp;
   if (!iomArgs.SessionFactoryPark_)
      iomArgs.SessionFactoryPark_.reset(new fon9::SessionFactoryPark{"FpSession"});
   iomArgs.SessionFactoryPark_->Add(new ExgOuchLineFactory(fon9::seed::SysEnv_GetLogFileFmtPath(*holder.Root_),
                                                           fon9::Named{"SetOuch"}));
   ExgOuchTradeSeedSP seed{new ExgOuchTradeSeed(iomArgs)};
   ExgOuchLineMgr&    lineMgr = seed->GetLineMgr();
   lineMgr.SetRptHandler(new ExgOuchRptLogger{});
   lineMgr.TokenGen_.Reset(tokenPrefix);
   if (!holder.Root_->Add(seed)) {
      holder.SetPluginsSt(fon9::LogLevel::Error, "SetOuchTrade|err=Dup name: ", name);
      return false;
   }
   holder.SetPluginsSt(fon9::LogLevel::Info, "SetOuchTrade|name=", name, "|io=", iomArgs.Result_);
   return true;
}

} // namespaces

extern "C" f9setouch_API fon9::seed::PluginsDesc f9p_SetOuchTrade;
static fon9::seed::PluginsPark f9pAutoPluginsReg{"SetOuchTrade", &f9p_SetOuchTrade};
fon9::seed::PluginsDesc f9p_SetOuchTrade{"", &f9setouch::SetOuchTrade_Start, nullptr, nullptr,};
