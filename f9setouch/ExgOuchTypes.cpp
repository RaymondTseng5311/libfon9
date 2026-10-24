// \file f9setouch/ExgOuchTypes.cpp
#include "f9setouch/ExgOuchTypes.hpp"
#include "fon9/RevPrint.hpp"
#include "fon9/StrTo.hpp"
#include "fon9/TimeStamp.hpp"

namespace f9setouch {

ExgOuchRequest::ExgOuchRequest(f9fmkt_RxKind rxKind) : base{rxKind} {
   memset(&this->RegulatoryData_, ' ', sizeof(this->RegulatoryData_));
}
ExgOuchRequest::~ExgOuchRequest() {
}
void ExgOuchRequest::RevPrint(fon9::RevBuffer& rbuf) const {
   fon9::RevPrint(rbuf, "OuchReq"
                  "|kind=", static_cast<char>(this->RxKind_),
                  "|token=", ToStrView(this->OrderToken_),
                  "|existing=", ToStrView(this->ExistingToken_),
                  "|obid=", this->OrderBookId_,
                  "|side=", static_cast<char>(this->Side_),
                  "|qty=", this->Qty_,
                  "|pri=", this->Pri_,
                  "|tif=", static_cast<unsigned>(this->TimeInForce_),
                  "|acc=", ToStrView(this->ClientAccount_));
}

//--------------------------------------------------------------------------//

OuchTokenGen::OuchTokenGen() {
   this->Reset(fon9::StrView{});
}
void OuchTokenGen::Reset(fon9::StrView prefix, uint64_t nextNo) {
   std::lock_guard<std::mutex> lk{this->Mutex_};
   if (prefix.size() > 4)
      prefix = fon9::StrView{prefix.begin(), 4};
   this->Prefix_ = prefix.ToString();
   this->NextNo_ = nextNo;
}
void OuchTokenGen::Reset(fon9::StrView prefix) {
   const fon9::TimeStamp now = fon9::LocalNow();
   const uint64_t        ms = static_cast<uint64_t>(fon9::GetDayTime(now).GetOrigValue() / 1000);
   this->Reset(prefix, ms * 1000);
}
OuchToken OuchTokenGen::Fetch() {
   char  buf[kOuchTokenSize];
   char* pout = buf + sizeof(buf);
   std::lock_guard<std::mutex> lk{this->Mutex_};
   const char* const pnum = buf + this->Prefix_.size();
   uint64_t          no = this->NextNo_++;
   while (pout != pnum) {
      *--pout = static_cast<char>('0' + (no % 10));
      no /= 10;
   }
   memcpy(buf, this->Prefix_.data(), this->Prefix_.size());
   return OuchToken{buf, sizeof(buf)};
}

//--------------------------------------------------------------------------//

ExgOuchRptHandler::~ExgOuchRptHandler() {
}
void ExgOuchRptHandler::OnOuchUnknown(ExgOuchTradingLine&, uint64_t, const void*, unsigned) {
}
void ExgOuchRptHandler::OnOuchNoReadyLine(f9fmkt::TradingRequest&, fon9::StrView) {
}

//--------------------------------------------------------------------------//

void ExgOuchLineArgs::Clear() {
   this->User_.clear();
   this->Pass_.clear();
   this->Session_.clear();
   this->SeqNo_ = 1;
   this->FcArgs_.Clear();
   this->LineFc_.FcCount_ = 0;
   this->LineFc_.FcTimeMS_ = 0;
   this->HbInterval_ = fon9::TimeInterval_Second(1);
   this->RxTimeout_ = fon9::TimeInterval_Second(15);
   this->IsNeedsLog_ = true;
}
fon9::ConfigParser::Result ExgOuchLineArgs::OnTagValue(fon9::StrView tag, fon9::StrView& value) {
   if (tag == "User")
      this->User_ = value.ToString();
   else if (tag == "Pass")
      this->Pass_ = value.ToString();
   else if (tag == "Session")
      this->Session_ = value.ToString();
   else if (tag == "SeqNo") {
      const char* pend;
      this->SeqNo_ = fon9::StrTo(value, uint64_t{0}, &pend);
      if (pend != value.end() || this->SeqNo_ == 0)
         return fon9::ConfigParser::Result::EInvalidValue;
   }
   else if (tag == "Hb")
      this->HbInterval_ = fon9::StrTo(value, fon9::TimeInterval{});
   else if (tag == "RxTimeout")
      this->RxTimeout_ = fon9::StrTo(value, fon9::TimeInterval{});
   else if (tag == "Log") {
      if (value.size() != 1)
         return fon9::ConfigParser::Result::EInvalidValue;
      this->IsNeedsLog_ = (fon9::toupper(value.Get1st()) == 'Y');
   }
   else if (fon9::iequals(tag, "LineFc"))
      return this->LineFc_.OnValue(value);
   else
      return this->FcArgs_.OnTagValue(tag, value);
   return fon9::ConfigParser::Result::Success;
}
std::string ExgOuchLineArgs::Verify() const {
   if (this->User_.empty() || this->User_.size() > kSoupUsernameSize)
      return "Invalid User: 1..6 chars";
   if (this->Pass_.empty() || this->Pass_.size() > kSoupPasswordSize)
      return "Invalid Pass: 1..10 chars";
   if (this->Session_.size() > kSoupSessionSize)
      return "Invalid Session: 0..10 chars";
   if (this->HbInterval_.GetOrigValue() <= 0)
      return "Invalid Hb";
   if (this->RxTimeout_ <= this->HbInterval_)
      return "Invalid RxTimeout: must > Hb";
   return std::string{};
}
f9setouch_API std::string ExgOuchLineArgsParser(ExgOuchLineArgs& args, fon9::StrView cfg) {
   return fon9::ParseFullConfig(args, cfg);
}

} // namespaces
