// \file f9setouch/ExgOuchLineMgr.hpp
#ifndef __f9setouch_ExgOuchLineMgr_hpp__
#define __f9setouch_ExgOuchLineMgr_hpp__
#include "f9setouch/ExgOuchTypes.hpp"
#include "fon9/fmkt/TradingLine.hpp"
#include "fon9/framework/IoManagerTree.hpp"

namespace f9setouch {

fon9_WARN_DISABLE_PADDING;
/// SET OUCH 下單線路管理員.
/// - 負責管理: 可用線路、下單時尋找適當線路、流量管制排隊(f9fmkt::TradingLineManager);
/// - 負責管理: 通訊設定、狀態log、通訊物件的生死(fon9::IoManagerTree);
/// - 提供 Order Token 產生器, 及回報處理者(RptHandler).
class f9setouch_API ExgOuchLineMgr : public f9fmkt::TradingLineManager
                                   , public fon9::IoManagerTree {
   fon9_NON_COPY_NON_MOVE(ExgOuchLineMgr);
   using baseTrading = f9fmkt::TradingLineManager;
   using baseIo = fon9::IoManagerTree;
   ExgOuchRptHandlerSP  RptHandler_;

protected:
   /// 轉給 RptHandler_->OnOuchNoReadyLine();
   f9fmkt::SendRequestResult NoReadyLineReject(f9fmkt::TradingRequest& req, fon9::StrView cause) override;

public:
   OuchTokenGen   TokenGen_;

   ExgOuchLineMgr(const fon9::IoManagerArgs& ioargs, fon9::TimeInterval afterOpen = fon9::TimeInterval_Second(1))
      : baseIo(ioargs, afterOpen) {
   }
   ~ExgOuchLineMgr();

   /// 必須在線路開啟前設定, 開啟後不可再改變.
   void SetRptHandler(ExgOuchRptHandlerSP rpt) {
      this->RptHandler_ = std::move(rpt);
   }
   ExgOuchRptHandler* GetRptHandler() const {
      return this->RptHandler_.get();
   }

   /// 清除排隊中的下單要求.
   void OnParentSeedClear() override;
};
fon9_WARN_POP;

} // namespaces
#endif//__f9setouch_ExgOuchLineMgr_hpp__
