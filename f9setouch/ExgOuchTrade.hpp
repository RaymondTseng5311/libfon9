// \file f9setouch/ExgOuchTrade.hpp
//
// Plugin "SetOuchTrade": 建立 SET OUCH 下單線路管理員, 並提供手動下單指令(PoC 測試用).
//
#ifndef __f9setouch_ExgOuchTrade_hpp__
#define __f9setouch_ExgOuchTrade_hpp__
#include "f9setouch/ExgOuchLineFactory.hpp"
#include "fon9/seed/MaTree.hpp"

namespace f9setouch {

/// 解析手動下單指令:
/// - new  OrderBookId Side Qty Price [Tif=0|3|4] [Acc=] [Token=] [SsQty=]
///   例: "new 1001 B 100 34.25 Acc=ACC001";
///   Side: B=買, S=賣, T=放空(若沒提供 SsQty, 則 SsQty=Qty);
/// - chg  ExistingToken Qty Price [Token=]
///   Qty: 期望的剩餘總量, 0=不變; Price: 0=不變;
/// - del  Token
/// retval == nullptr 表示失敗, 失敗原因在 err.
f9setouch_API ExgOuchRequestSP MakeOuchRequest(fon9::StrView cmdln, std::string& err);

/// 預設的回報處理: 用 fon9_LOG_INFO 記錄回報內容.
class f9setouch_API ExgOuchRptLogger : public ExgOuchRptHandler {
   fon9_NON_COPY_NON_MOVE(ExgOuchRptLogger);
   void LogRpt(uint64_t seqNo, const void* msg, unsigned msgsz);
public:
   ExgOuchRptLogger() = default;
   void OnOuchAccepted(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderAccepted& rpt) override;
   void OnOuchRejected(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderRejected& rpt) override;
   void OnOuchReplaced(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderReplaced& rpt) override;
   void OnOuchCancelled(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderCancelled& rpt) override;
   void OnOuchExecuted(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderExecuted& rpt) override;
   void OnOuchUnknown(ExgOuchTradingLine& line, uint64_t seqNo, const void* msg, unsigned msgsz) override;
   void OnOuchNoReadyLine(f9fmkt::TradingRequest& req, fon9::StrView cause) override;
};

/// 下單線路管理員的 seed.
/// - Sapling_ = ExgOuchLineMgr;
/// - SeedCommand: 請參考 MakeOuchRequest(); 結果為 "token=...|result=Sent";
class f9setouch_API ExgOuchTradeSeed : public fon9::seed::NamedSapling {
   fon9_NON_COPY_NON_MOVE(ExgOuchTradeSeed);
   using base = fon9::seed::NamedSapling;
public:
   ExgOuchTradeSeed(const fon9::IoManagerArgs& ioargs);
   ~ExgOuchTradeSeed();

   ExgOuchLineMgr& GetLineMgr() const {
      return *static_cast<ExgOuchLineMgr*>(this->Sapling_.get());
   }
   void OnSeedCommand(fon9::seed::SeedOpResult& res, fon9::StrView cmdln, fon9::seed::FnCommandResultHandler resHandler,
                      fon9::seed::MaTreeBase::Locker&& ulk, fon9::seed::SeedVisitor* visitor) override;

   /// 送出下單要求, 返回結果說明, 例: "token=...|result=Sent";
   std::string SendRequest(ExgOuchRequest& req);
};
using ExgOuchTradeSeedSP = fon9::intrusive_ptr<ExgOuchTradeSeed>;

} // namespaces
#endif//__f9setouch_ExgOuchTrade_hpp__
