rm -rf /mnt/nova/*

filebench -f mywebserver0.f &
filebench -f mywebserver1.f &
filebench -f mywebserver2.f &
filebench -f mywebserver3.f 
