#include "PetLinkCore.h"
#include "PetLinkDrive.h"
#include <assert.h>
#include <stdio.h>

using namespace petlink;
static void driveChecks() {
  const int16_t axes[]={-1000,-500,-200,-20,-1,0,1,20,200,500,1000};
  for(int16_t linear:axes) for(int16_t angular:axes) {
    const auto intent=directDriveIntent(linear,angular);
    assert(intent.linear==linear && intent.angular==angular); // 不设主次、不裁掉斜向输入。
  }
  assert(fabsf(1000*kRingLinearScale-1.0f)<0.000001f);
  assert(fabsf(1000*kRingAngularScale-1.0f)<0.000001f);
  const auto reverse=directDriveIntent(-1000,0);
  const auto left=directDriveIntent(0,1000);
  assert(reverse.linear*kRingLinearScale==-1.0f && reverse.angular==0);
  assert(left.linear==0 && left.angular*kRingAngularScale==1.0f);
  const DriveIntent paused;
  assert(paused.linear==0 && paused.angular==0);
  assert(driveClamp(-2000)==-1000 && driveClamp(2000)==1000);
}
int main() {
  driveChecks();
  uint8_t bytes[kMaxFrame]{};
  Packet p{};
  assert(header(bytes,Type::Control,0xFEDCBA9876543210ULL,0xFFFFFFFE,16,true)==36);
  put64(bytes+20,99); put16(bytes+28,uint16_t(-1000)); put16(bytes+30,1000);
  assert(decode(bytes,52,p));
  assert(p.type==Type::Control && p.authenticated && p.length==16);
  assert(p.session==0xFEDCBA9876543210ULL && p.seq==0xFFFFFFFE);
  assert(get64(p.data)==99 && int16_t(get16(p.data+8))==-1000);
  assert(!decode(bytes,51,p));
  bytes[6]=2; assert(!decode(bytes,52,p)); bytes[6]=1;
  bytes[2]=1; assert(!decode(bytes,52,p));
  bytes[2]=2; assert(!decode(bytes,52,p)); bytes[2]=kProtocolVersion;
  bytes[3]=255; assert(!decode(bytes,52,p));
  assert(!decode(nullptr,52,p));
  assert(header(bytes,Type::Pet,1,1,kMaxFrame,true)==0);
  assert(newer(0,0xFFFFFFFFU));
  assert(!newer(4,4) && !newer(4,5));
  assert(header(bytes,Type::LinkReady,42,1,8,true)==28); put64(bytes+20,88);
  assert(decode(bytes,44,p) && p.type==Type::LinkReady && get64(p.data)==88);
  assert(header(bytes,Type::Goodbye,42,2,0,true)==20);
  assert(decode(bytes,36,p) && p.type==Type::Goodbye);

  Lease lease;
  assert(!lease.accepts(1,0));
  assert(lease.grant(11,22,100));
  assert(lease.accepts(22,399));
  assert(!lease.accepts(22,400));
  assert(!lease.accepts(21,150));
  lease.stop(true);
  assert(!lease.accepts(22,150));
  assert(!lease.grant(12,23,151));
  lease.locked=false;
  assert(!lease.grant(11,23,151)); // 旧授权申请不能复活。
  assert(lease.grant(12,23,151));
  assert(!lease.accepts(22,152));
  lease.stop();
  assert(!lease.accepts(23,152));
  assert(lease.grant(13,24,0xFFFFFFF0U));
  assert(lease.accepts(24,0x00000020U));
  assert(!lease.accepts(24,0x00000120U));

  Tilt tilt;
  uint32_t now=1000;
  tilt.sample(0,0,1000,0,0,0,now);
  now+=30; tilt.sample(0,0,1000,0,0,0,now);
  assert(!tilt.calibrated()); // 进入页面不会偷偷采用桌面姿态。
  assert(tilt.capture(now));
  assert(tilt.neutral(now) && tilt.linear()==0 && tilt.angular()==0);
  assert(tilt.rawAge(now)==0 && tilt.age(now)==0);
  assert(!tilt.ready(now+80) && !tilt.expired(now+179) && tilt.expired(now+180));
  for(int i=0;i<30;++i) { now+=30; tilt.sample(500,0,866,0,0,0,now); }
  assert(tilt.ready(now) && tilt.angular()>700 && tilt.linear()==0);
  // 旧参考之外也可点击取零，不需要回到旧水平。
  assert(tilt.capture(now)); assert(tilt.neutral(now) && tilt.angular()==0);
  for(int i=0;i<40;++i) { now+=30; tilt.sample(-500,0,866,0,0,0,now); }
  assert(tilt.ready(now) && tilt.angular()==-1000); // 常规大角度限幅而非断开。
  now+=30; const uint32_t before=now-30;
  assert(!tilt.sample(0,0,1000,400000,0,0,now));
  assert(!tilt.ready(now) && !tilt.expired(now) && tilt.angular()==0);
  assert(tilt.reason(now)==InputReason::AngularSpeed && tilt.age(now)==now-before);
  now+=30; tilt.sample(0,0,1000,0,0,0,now);
  assert(!tilt.ready(now)); // 两个可靠样本之后才恢复，参考点不变。
  now+=30; tilt.sample(0,0,1000,0,0,0,now);
  assert(tilt.ready(now) && tilt.angular()<0);
  now+=30; assert(!tilt.sample(0,0,2000,0,0,0,now));
  assert(!tilt.ready(now) && tilt.expired(now+150));
  tilt.reset();
  assert(!tilt.ready(now) && !tilt.capture(now));
  // 新佩戴姿态即使与旧姿态反向，也能在新的显式开始中重新取零。
  for(int i=0;i<4;++i) { now+=30; tilt.sample(0,0,-1000,0,0,0,now); }
  assert(tilt.capture(now) && tilt.neutral(now));
  const uint32_t lastUsable=now;
  for(int i=0;i<6;++i) {
    now+=30;
    if(i%2==0) tilt.sample(0,0,2000,0,0,0,now);
    else tilt.sample(0,0,-1000,0,0,0,now);
  }
  assert(!tilt.ready(now) && tilt.expired(now) && now-lastUsable==kInputTimeout);
  puts("PetLink core checks passed");
  return 0;
}
