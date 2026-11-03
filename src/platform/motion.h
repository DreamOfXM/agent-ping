/* 平台层：这台机器的「减弱动效」开没开。
   core 只认这个 C 接口，不许 include 任何平台头（与 lang.h / cursor.h 同规矩）。 */
#ifndef AGENTPING_MOTION_H
#define AGENTPING_MOTION_H

#ifdef __cplusplus
extern "C" {
#endif

/* 1＝用户要求减弱动效，0＝没要求。
   src 写回「这个值从哪儿读的」或「读不到」——读不到不等于用户没开，两件事必须分开报
   （与 lang.h 同一规矩：把「没读到」当成「读到否」是假通过，出了问题屏上看不出来）。
   调用方在读不到时按 0 处理，动效照常。
   这一层只读系统偏好，绝不写：无障碍设置归用户。 */
int ap_reduce_motion(char* src, int out_len);

#ifdef __cplusplus
}
#endif
#endif
