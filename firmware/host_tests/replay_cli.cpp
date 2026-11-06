// Runs the FrostCore engine over a replay CSV on the PC, exactly as the node does in REPLAY mode.
// stdin : ts,t1,rh1,ok1,t2,rh2,ok2,time_valid   (header line optional)
// stdout: ts,t,rh,td,d,t_smooth,rt_1h,rd_1h,rt_3h,quality,level,flags
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../libraries/FrostCore/src/FrostCore.h"

int main() {
  frost::Engine e;
  char line[256];
  printf("ts,t,rh,td,d,t_smooth,rt_1h,rd_1h,rt_3h,quality,level,flags\n");
  while (fgets(line, sizeof(line), stdin)) {
    if (line[0] < '0' || line[0] > '9') continue;  // header
    long long ts;
    double t1, rh1, t2, rh2;
    int ok1, ok2, tv;
    if (sscanf(line, "%lld,%lf,%lf,%d,%lf,%lf,%d,%d", &ts, &t1, &rh1, &ok1, &t2, &rh2, &ok2, &tv) != 8) {
      fprintf(stderr, "bad line: %s", line);
      return 2;
    }
    frost::Reading a, b;
    a.t = t1; a.rh = rh1; a.ok = ok1 != 0;
    b.t = t2; b.rh = rh2; b.ok = ok2 != 0;
    frost::Output o = e.update(ts, a, b, tv != 0);
    printf("%lld,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%u\n", (long long)o.ts, o.t, o.rh, o.td, o.d,
           o.tSmooth, o.rt1h, o.rd1h, o.rt3h, o.quality, o.level, o.flags);
  }
  return 0;
}
