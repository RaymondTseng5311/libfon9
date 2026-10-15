// \file f9set/ExgMdSystem.hpp
#ifndef __f9set_ExgMdSystem_hpp__
#define __f9set_ExgMdSystem_hpp__
#include "f9set/ExgMdRtsBuilder.hpp"
#include "fon9/fmkt/MdSystem.hpp"
#include "fon9/io/Device.hpp"
#include "fon9/framework/IoManager.hpp"

namespace f9set {

/// SET 行情系統(提供給 fon9 MdRts 發行).
/// SetMdSys
///   ├─ Symbs       商品資料表(ExgMdSymbs).
///   └─ {Name}_io   行情來源(ExgMdReceiverFactory 建立的 Session):
///                  - Mc:      MoldUDP64 multicast, 可設定多條(A/B 雙路), 由序號過濾重複;
///                  - Rr:      MoldUDP64 Re-request(UDP), 缺號時送出回補要求, 回覆同樣由序號整合;
///                  - Glimpse: GLIMPSE 快照(SoupBinTCP), 連線成功後取得快照, 完成後斷線.
/// - 全部來源都在同一個 ExgMdSystem 裡面, 由 ExgMdRtsBuilder(加鎖) 依序號整合.
/// - 缺號:
///   - 先等 RecoverArgs_.GapWait_(另一路可能補上), 然後透過 Rr 送出回補要求;
///   - 沒有 Rr, 或多次要求仍未補上: 放棄缺號(委託簿可能不正確),
///     若有設定 Glimpse, 則重新取得快照(IsSnapshotOnLost_).
class f9set_API ExgMdSystem : public fon9::fmkt::MdSystemWithHb {
   fon9_NON_COPY_NON_MOVE(ExgMdSystem);
   using base = fon9::fmkt::MdSystemWithHb;

   class Builder;
   friend class Builder;
   std::unique_ptr<Builder>   Builder_;
   std::mutex                 Mutex_;

   fon9::io::DeviceSP         RrDevice_;
   fon9::io::DeviceSP         GlimpseDevice_;
   bool                       IsGlimpseRequested_{false};
   uint64_t                   LastHbPkCount_{0};

   static void EmitOnRecoverTimer(fon9::TimerEntry* timer, fon9::TimeStamp now);
   using RecoverTimer = fon9::DataMemberEmitOnTimer<&ExgMdSystem::EmitOnRecoverTimer>;
   RecoverTimer   RecoverTimer_;

   void OnMdClearTimeChanged() override;
   void OnMdSystemStartup(unsigned tdayYYYYMMDD, const std::string& logPath) override;
   void OnParentTreeClear(fon9::seed::Tree& parent) override;
   fon9::TimeInterval OnMdSystem_HbTimer(fon9::TimeStamp now) override;

   ExgMdSystem(fon9::seed::MaTreeSP root, std::string name, std::string rtiNamePre, bool isAddMarketSeq);

public:
   const ExgMdSymbsSP   Symbs_;
   /// 放棄缺號後, 是否重新取得 GLIMPSE 快照, 預設為 true.
   bool                 IsSnapshotOnLost_{true};

   /// useRtiForRecover: 是否將即時資料存檔, 提供訂閱者回補.
   ExgMdSystem(fon9::seed::MaTreeSP root, std::string name, bool useRtiForRecover, bool isAddMarketSeq);
   ~ExgMdSystem();

   /// 建立 IoManager: this->Name_ + "_io", 並加入 "SetMdRecv" Session Factory.
   /// iomArgs.DeviceFactoryPark_ 若為 nullptr, 則使用 root 的 "FpDevice".
   fon9::IoManagerSP PlantIoMgr(fon9::seed::MaTree& root, fon9::IoManagerArgs& iomArgs);

   /// 設定交易所時區、回補參數; 必須在啟動前設定.
   void SetTimeZoneOffset(fon9::TimeInterval ti);
   void SetRecoverArgs(const MoldUdp64RecoverArgs& args);

   // ----- 底下提供給 Session 使用, thread safe.
   /// 收到一個 MoldUDP64 封包(Mc 或 Rr).
   MoldUdp64Result OnMoldPacket(const void* pk, size_t pksz);
   /// Rr 連線可用時設定, 斷線時清除(dev=nullptr 時, 只有 cur==RrDevice_ 才清除).
   void SetRrDevice(fon9::io::Device* dev, fon9::io::Device* cur = nullptr);
   void SetGlimpseDevice(fon9::io::Device* dev, fon9::io::Device* cur = nullptr);
   /// GLIMPSE 連線成功, 開始取得快照.
   void GlimpseBegin();
   /// GLIMPSE 的 Sequenced Data.
   /// \retval true 快照已完成(收到 'G').
   bool GlimpseMessage(uint64_t seqNo, const void* msg, unsigned msgsz);
   /// GLIMPSE 斷線(尚未收到 'G'), 放棄快照.
   void GlimpseAbort();
   bool IsWaitingSnapshot();

   /// 狀態字串, 例: "|pk=...|msg=...|gap=..."
   std::string GetInfo();
   /// 在鎖定狀態下操作 builder(測試、查詢用).
   template <class Fn>
   auto LockBuilder(Fn&& fn) -> decltype(fn(*static_cast<ExgMdRtsBuilder*>(nullptr))) {
      std::lock_guard<std::mutex> lk{this->Mutex_};
      return fn(this->GetBuilder());
   }

private:
   ExgMdRtsBuilder& GetBuilder();
   void CheckGlimpseRequest();
};
using ExgMdSystemSP = fon9::intrusive_ptr<ExgMdSystem>;

} // namespaces
#endif//__f9set_ExgMdSystem_hpp__
