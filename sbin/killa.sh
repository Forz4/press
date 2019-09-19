#! /bin/bash
ps -ef  | grep press | grep -v grep | awk '{print $2}' |xargs kill -9
