// \file f9setouch/ExgOuchTypes.hpp
//
// SET OUCH 下單要求、回報處理介面、Order Token 產生器、線路參數.
//
#ifndef __f9setouch_ExgOuchTypes_hpp__
#define __f9setouch_ExgOuchTypes_hpp__
#include "f9setouch/OuchFmt.hpp"
#include "f9setouch/OuchSoupBinTcp.hpp"
#include "fon9/fmkt/TradingRequest.hpp"
#include "fon9/ConfigParser.hpp"
#include "fon9/FlowCounter.hpp"
#include "fon9/MustLock.hpp"

fon9_BEFORE_INCLUDE_STD
#include <mutex>
#include <string>
fon9_AFTER_INCLUDE_STD

namespace f9setouch {
namespace f9fmkt = fon9::fmkt;

class f9setouch_API ExgOuchTradingLine;

fon9_WARN_DISABLE_PADDING;
/// SET OUCH 下單要求.
/// - RxKind() 決定送出的 OUCH 訊息:
///   - f9fmkt_RxKind_RequestNew:    Enter Order('O');
///   - f9fmkt_RxKind_RequestChgQty, f9fmkt_RxKind_RequestChgPri: Replace Order('U');
///   - f9fmkt_RxKind_RequestDelete: Cancel Order('X');
/// - OrderToken_ 若為空白, 送出時由 ExgOuchLineMgr 的 Token 產生器填入.
class f9setouch_API ExgOuchRequest : public f9fmkt::TradingRequest {
   fon9_NON_COPY_NON_MOVE(ExgOuchRequest);
   using base = f9fmkt::TradingRequest;

protected:
   ~ExgOuchRequest();

public:
   /// - New:     新單的 Token;
   /// - Replace: Replacement Order Token(改單後的新 Token);
   /// - Delete:  新單的 Token(不是改單的 Replacement Token);
   OuchToken            OrderToken_{nullptr};
   /// Replace: 委託目前(最後一次改單成功)的 Token.
   OuchToken            ExistingToken_{nullptr};
   /// 商品代號: ITCH Order Book Directory 的 Order Book ID.
   uint32_t             OrderBookId_{0};
   OuchSide             Side_{OuchSide::Buy};
   OuchTimeInForce      TimeInForce_{OuchTimeInForce::Day};
   OuchOrderType        OrderType_{OuchOrderType::Limit};
   /// - New:     委託數量(股);
   /// - Replace: 期望的「剩餘總量」, 0 表示不變;
   f9fmkt::Qty          Qty_{0};
   /// - Replace: 0 表示不變;
   OuchPrice            Pri_{};
   /// 放空數量: Side=ShortSell 時必須 > 0.
   f9fmkt::Qty          ShortSellQty_{0};
   f9fmkt::Qty          MinAcceptQty_{0};
   OuchAlpha<10>        ClientAccount_{nullptr};
   OuchAlpha<15>        CustomerInfo_{nullptr};
   OuchAlpha<32>        ExchangeInfo_{nullptr};
   OuchAlpha<1>         ClearingParticipant_{nullptr};
   OuchRegulatoryData   RegulatoryData_;

   explicit ExgOuchRequest(f9fmkt_RxKind rxKind);

   bool IsReplace() const {
      return this->RxKind_ == f9fmkt_RxKind_RequestChgQty
         || this->RxKind_ == f9fmkt_RxKind_RequestChgPri;
   }
   void RevPrint(fon9::RevBuffer& rbuf) const override;
};
using ExgOuchRequestSP = fon9::intrusive_ptr<ExgOuchRequest>;

//--------------------------------------------------------------------------//

/// Order Token 產生器: Prefix + 數字(右靠左補 '0'), 共 kOuchTokenSize 碼.
/// - 每個 OUCH 帳號, 當日的 Token 不可重複.
/// - 初始值 = 本地時間(當日)的毫秒數 * 1000, 程式重啟後(只要每毫秒少於 1000 筆)不會重複.
class f9setouch_API OuchTokenGen {
   fon9_NON_COPY_NON_MOVE(OuchTokenGen);
   std::mutex  Mutex_;
   std::string Prefix_;
   uint64_t    NextNo_{0};
public:
   OuchTokenGen();
   /// prefix 最多 4 碼.
   void Reset(fon9::StrView prefix, uint64_t nextNo);
   void Reset(fon9::StrView prefix);
   OuchToken Fetch();
};

//--------------------------------------------------------------------------//

/// 處理 OUCH 回報.
/// - 在線路的 io thread 呼叫, 同一條線路依序號呼叫.
/// - 重新登入時, 從上次的序號接續; 程式重啟時, 從 SeqNo(預設1) 開始, 會收到當日全部的回報.
///   因此回報處理者必須能處理重複的回報(例如: 用 seqNo 判斷).
class f9setouch_API ExgOuchRptHandler : public fon9::intrusive_ref_counter<ExgOuchRptHandler> {
   fon9_NON_COPY_NON_MOVE(ExgOuchRptHandler);
public:
   ExgOuchRptHandler() = default;
   virtual ~ExgOuchRptHandler();

   virtual void OnOuchAccepted(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderAccepted& rpt) = 0;
   virtual void OnOuchRejected(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderRejected& rpt) = 0;
   virtual void OnOuchReplaced(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderReplaced& rpt) = 0;
   virtual void OnOuchCancelled(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderCancelled& rpt) = 0;
   virtual void OnOuchExecuted(ExgOuchTradingLine& line, uint64_t seqNo, const OuchOrderExecuted& rpt) = 0;
   /// 無法辨識(或長度不正確)的訊息, 預設 do nothing.
   virtual void OnOuchUnknown(ExgOuchTradingLine& line, uint64_t seqNo, const void* msg, unsigned msgsz);
   /// 沒有可用線路, 拒絕下單要求, 預設 do nothing.
   virtual void OnOuchNoReadyLine(f9fmkt::TradingRequest& req, fon9::StrView cause);
};
using ExgOuchRptHandlerSP = fon9::intrusive_ptr<ExgOuchRptHandler>;

//--------------------------------------------------------------------------//

/// OUCH 下單線路參數.
/// cfg = "User=|Pass=|Session=|SeqNo=|Fc=筆數/ms|LineFc=筆數/ms|Hb=秒|RxTimeout=秒|Log=Y";
/// - User, Pass: 必須提供, SoupBinTCP 的 Username(最多6碼), Password(最多10碼).
/// - Session: 要登入的 Session, 預設空白(目前的 Session).
/// - SeqNo:   首次登入時要求的序號, 預設 1(從頭取得當日全部回報); 重新連線時會接續上次的序號.
/// - Fc:      交易所流量管制, 預設不管制.
/// - LineFc:  TCP 流量管制(參考 fon9::fmkt::TradingLine).
/// - Hb:      多久沒送出資料就送 Client Heartbeat, 預設 1 秒.
/// - RxTimeout: 多久沒收到資料就斷線, 預設 15 秒.
/// - Log:     是否記錄收送訊息, 預設 Y.
struct f9setouch_API ExgOuchLineArgs {
   std::string             User_;
   std::string             Pass_;
   std::string             Session_;
   uint64_t                SeqNo_;
   fon9::FlowCounterArgs   FcArgs_;
   fon9::FlowCounterArgs   LineFc_;
   fon9::TimeInterval      HbInterval_;
   fon9::TimeInterval      RxTimeout_;
   bool                    IsNeedsLog_;

   void Clear();
   fon9::ConfigParser::Result OnTagValue(fon9::StrView tag, fon9::StrView& value);
   std::string Verify() const;
};
fon9_WARN_POP;

/// retval.empty() 成功, 否則為錯誤訊息.
f9setouch_API std::string ExgOuchLineArgsParser(ExgOuchLineArgs& args, fon9::StrView cfg);

} // namespaces
#endif//__f9setouch_ExgOuchTypes_hpp__
