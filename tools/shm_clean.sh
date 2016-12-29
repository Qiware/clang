#!/bin/sh

###############################################################################
## Copyright(C) 2014-2024 Qiware technology Co., Ltd
##
## 功    能: 清理未使用的共享内存
## 注意事项: 
##		请勿随意修改此文件
## 作    者: # Qifeng.zou # 2014.09.04 #
###############################################################################

shmlist=`ipcs -m | awk '{ if($6 ~ /0/) { print $2 } }'`                            

for key in $shmlist                                                                
do                                                                              
    echo $key                                                                   
    ipcrm -m $key                                                               
done 