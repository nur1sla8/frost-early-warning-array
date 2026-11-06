"""CSV column sets shared by the PC tools (must match the firmware printf formats)."""

NODE_HEADER = ("unix_ts,node_id,boot_id,seq,uptime_s,t1,rh1,ok1,t2,rh2,ok2,t,rh,td,tf,d,t_smooth,rt_1h,rd_1h,"
               "rt_3h,quality,level,level_name,flags,send_fail_streak,calibrated,time_valid")
GW_HEADER = ("gw_unix_ts,node_unix_ts,node_id,boot_id,seq,uptime_s,t1,rh1,t2,rh2,t,rh,td,d,t_smooth,rt_1h,"
             "rd_1h,rt_3h,quality,level,level_name,flags,calibrated,send_fail_streak,rssi,lost,duplicates")
