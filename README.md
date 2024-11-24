# snac2-sunos

This is SNAC2 for Solaris. I am already assuming that you know how to set up snac2. If you don't, go practice on Linux or something with real snac2, then come back here.

## Installation
Before doing anything, go install lighttpd. You can do that with `pkg install lighttpd` to install it from an IPS repo, or you can 
go build it yourself. Note that this guide is using the lighttpd 1.4 that is distributed with Solaris 11.4, and it relies on an 
antique version of OpenSSL (1.0.1). You do absolutely **NOT** want to do this with anything sitting out on the internet, so exercise 
caution accordingly using this old version. 

You also need a compiler. I am using Sun Studio (well, Oracle Studio) 12.6. That compiler works on Solaris 10 and 11, and can output
32 and 64 bit executables (just use the version for SPARC or x86, whatever you're running). Make sure you install and configure that
correctly, then roll on with the dependency installation if you haven't done so already.

Once you've done that, prepare the system as follows:

As root:
```
# adduser snac
# passwd snac
# mkdir /dpool/snacdata
# vi /etc/passwd
snac:x:100:10::/dpool/snacdata:/usr/bin/bash
# git clone https://github.com/hackersmacker/snac2-solaris.git
# cd snac2-solaris
# make
# cp snac /dpool/snacdata
# chown -r /dpool/snacdata snac
```

As snac:
```
$ ./snac init /dpool
$ vi server.json
"fastcgi": "true"
$ ./snac adduser /dpool/snacdata/ joeblow
$ ./snac httpd /dpool/snacdata/
```

As root:
```
# vi /etc/lighttpd/1.4/lighttpd.conf
server.modules += ( "mod_fastcgi" )
fastcgi.server = (
  "/" =>
  (( "host" => "127.0.0.1",
     "port" => 8001,
     "check-local" => "disable",
     "docroot" => "/"
  ))
)
# /usr/lighttpd/1.4/sbin/lighttpd -f /etc/lighttpd/1.4/lighttpd.conf
```

At this point, you should have a functional SNAC2 instance for Solaris. Configuration and usage is standard, but you may want to
ensure that you have updated the root CA certs -- this is pretty easy to do. Get a set of root certs from Mozilla, and place them
in `/etc/openssl/certs`. If you run into trouble, you may wish to grab the NetBSD `security/ca-certificates` package, and extract
all of the certs from that. Once you do that, restart SNAC2. 

Good luck!

## Author

grunfink [@grunfink@comam.es](https://comam.es/snac/grunfink) with the help of others.

hackersmacker ported this to SunOS, hardly expending any effort.

Buy grunfink a coffee: https://ko-fi.com/grunfink
