// \file f9set/ExgMdReceiverFactory.cpp
#include "f9set/ExgMdReceiverFactory.hpp"
#include "f9set/ExgMdReceiverSession.hpp"

namespace f9set {
using namespace fon9;

io::SessionSP ExgMdReceiverFactory::CreateSession(IoManager& ioMgr, const IoConfigItem& cfg, std::string& errReason) {
   (void)ioMgr;
   StrView  tag, value, args = ToStrView(cfg.SessionArgs_);
   StrView  type{"Mc"}, pkLogName, user, pass;
   while (StrFetchTagValue(args, tag, value)) {
      if (tag == "Type")
         type = value;
      else if (tag == "PkLog")
         pkLogName = value;
      else if (tag == "User")
         user = value;
      else if (tag == "Pass")
         pass = value;
      else {
         errReason = "f9set.ExgMdReceiverFactory.CreateSession: Unknown tag:" + tag.ToString();
         return nullptr;
      }
   }
   if (iequals(type, "Mc"))
      return new ExgMdMoldSession(this->MdSys_, false, pkLogName);
   if (iequals(type, "Rr"))
      return new ExgMdMoldSession(this->MdSys_, true, pkLogName);
   if (iequals(type, "Glimpse"))
      return new ExgMdGlimpseSession(this->MdSys_, user, pass);
   errReason = "f9set.ExgMdReceiverFactory.CreateSession: Unknown Type=" + type.ToString();
   return nullptr;
}
io::SessionServerSP ExgMdReceiverFactory::CreateSessionServer(IoManager& ioMgr, const IoConfigItem& cfg, std::string& errReason) {
   (void)ioMgr; (void)cfg;
   errReason = "f9set.ExgMdReceiverFactory: Not support server.";
   return nullptr;
}

} // namespaces
