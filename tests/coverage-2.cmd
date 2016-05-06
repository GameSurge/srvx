define srv1 irc.clan-dk.org:7701
define srv1name irc.clan-dk.org
define srv2 irc.clan-dk.org:7711
define srv2name irc2.clan-dk.org
define srvx srvx.clan-dk.org
define domain troilus.org
define chanserv AlphaIRC
define global AlphaIRC
define memoserv AlphaIRC
define nickserv AlphaIRC
define opserv AlphaIRC
define helpserv CoverageServ
define helpserv2 C0v3r4g3S3rv
define opernick test_oper
define operpass i_r_teh_0p3r
define testchan #testchan

# Connect, join testing channel, oper up, log in
connect cl1 test1 test1 %srv1% :Test Bot 1
:cl1 join %testchan%1
:cl1 raw :OPER %opernick% %operpass%
:cl1 privmsg %nickserv% :ACCOUNTINFO
:cl1 privmsg %nickserv%@%srvx% :AUTH
:cl1 privmsg %nickserv%@%srvx% :AUTH bogus bogus
:cl1 privmsg %nickserv%@%srvx% :AUTH testest
:cl1 privmsg %nickserv% :OSET test1 EPITHET some damn test bot
:cl1 privmsg %nickserv% :ACCOUNTINFO

# Test common infrastructure things
:cl1 nick test1_new
:cl1 nick test1
:cl1 privmsg %opserv% :REHASH
:cl1 privmsg %opserv% :REOPEN
:cl1 privmsg %opserv% :QUERY
:cl1 privmsg %opserv% :LOG LIMIT 30
:cl1 privmsg %opserv% :RECONNECT
:cl1 privmsg %opserv% :HELP WRITE
:cl1 privmsg %opserv% :WRITE MONDO
:cl1 privmsg %opserv% :WRITEALL
:cl1 privmsg %opserv% :STATS DATABASES

# Test global's functionality
:cl1 privmsg %global% :NOTICE users Hello world!
:cl1 privmsg %global% :MESSAGE TARGET users DURATION 1h TEXT Hello world (short duration)!
connect cl2 test2 test2 %srv1% :Test Bot 2
connect cl3 test3 test3 %srv1% :Test Bot 3
:cl2 join %testchan%1
:cl2 privmsg %nickserv%@%srvx% :REGISTER test2 testest
:cl2 privmsg %global% :LIST
:cl3 join %testchan%1
:cl3 privmsg %global% :MESSAGES
:cl3 privmsg %global% :VERSION
:cl1 wait cl2,cl3
:cl1 privmsg %global% :REMOVE 1
:cl1 privmsg %global% :MESSAGE SOURCELESS pizza TARGET all TARGET helpers TARGET opers TARGET staff TARGET channels DURATION 5s TEXT Hollow world (very short duration).
:cl1 privmsg %global% :MESSAGE TARGET all
:cl1 privmsg %global% :NOTICE ANNOUNCEMENT test of announcement code
:cl1 privmsg %global% :NOTICE CHANNELS test of channel spamming code (sorry! :)
:cl1 privmsg %global% :NOTICE BOGUS
:cl1 privmsg %global% :NOTICE DIFFERENTLY BOGUS
:cl1 privmsg %global% :LIST
:cl1 privmsg %global% :REMOVE 30
:cl1 privmsg %global% :MESSAGES

# Test ChanServ functions
:cl1 privmsg %chanserv% :HELP
:cl1 privmsg %chanserv% :HELP commands
:cl1 privmsg %chanserv% :HELP note types
:cl1 privmsg %chanserv% :VERSION ARCH
:cl1 privmsg %chanserv% :NETINFO
:cl1 privmsg %chanserv% :STAFF
:cl1 privmsg %chanserv% :GOD ON
:cl1 privmsg %chanserv% :REGISTER %testchan%1
:cl1 privmsg %chanserv% :REGISTER %testchan%2 test2
:cl1 privmsg %chanserv% :GOD OFF
:cl1 privmsg %chanserv% :ADDUSER %testchan%1 OP test2
:cl1 privmsg %chanserv% :GOD ON
:cl1 privmsg %testchan%1 :PING
:cl1 privmsg %chanserv% :CREATENOTE url setter all 400
:cl1 privmsg %chanserv% :%testchan%1 NOTE url http://www.srvx.net/index.php
:cl1 privmsg %chanserv% :CREATENOTE url privileged 1 privileged 20
:cl1 privmsg %chanserv% :CREATENOTE url channel owner channel_users 20
:cl1 privmsg %chanserv% :CREATENOTE url bogus all 20
:cl1 privmsg %chanserv% :%testchan%1 NOTE
:cl1 privmsg %chanserv% :REMOVENOTE url
:cl2 wait cl1
:cl2 privmsg %chanserv% :%testchan%1 NOTE
:cl1 privmsg %chanserv% :REMOVENOTE bogus
:cl1 privmsg %chanserv% :%testchan%1 DELNOTE bogus
:cl1 privmsg %chanserv% :%testchan%1 DELNOTE url
:cl1 privmsg %chanserv% :%testchan%1 NOTE url http://www.srvx.net/
:cl1 wait cl2
:cl1 privmsg %chanserv% :REMOVENOTE url FORCE
:cl1 privmsg %chanserv% :%testchan%1 ADDUSER OP test2
:cl1 privmsg %chanserv% :%testchan%1 OP test2
:cl1 privmsg %chanserv% :%testchan%1 OP test3
:cl2 wait cl1
:cl2 mode %testchan%1 -clo test3
:cl1 privmsg %chanserv% :%testchan%1 SET MODES +sntlrcCDk 500 bah
:cl1 privmsg %chanserv% :%testchan%1 SET MODES -lk
:cl1 privmsg %chanserv% :%testchan%1 SET ENFMODES 4
:cl1 privmsg %chanserv% :%testchan%1 SET PROTECT 0
:cl2 wait cl1
:cl2 mode %testchan%1 +l 600
:cl1 wait cl2
:cl1 privmsg %chanserv% :%testchan%1 SET CTCPUSERS 6
:cl3 wait cl1
:cl3 privmsg %testchan%1 :TIME
:cl1 privmsg %chanserv% :EXPIRE
:cl2 privmsg %chanserv% :%testchan%1 DELETEME a5bfa227
:cl1 privmsg %chanserv% :NOREGISTER *test2 USUX
:cl1 privmsg %chanserv% :NOREGISTER %testchan%3 USUX2
:cl1 privmsg %chanserv% :NOREGISTER #*tch* USUX3
:cl1 privmsg %chanserv% :NOREGISTER %testchan%3
:cl1 privmsg %chanserv% :NOREGISTER *test2
:cl1 privmsg %chanserv% :NOREGISTER *test194
:cl1 privmsg %chanserv% :NOREGISTER
:cl1 privmsg %chanserv% :REGISTER %testchan%3 test2
:cl1 privmsg %chanserv% :ALLOWREGISTER
:cl1 privmsg %chanserv% :ALLOWREGISTER *test2
:cl1 privmsg %chanserv% :REGISTER %testchan%3 test2
:cl1 privmsg %chanserv% :ALLOWREGISTER %testchan%3
:cl1 privmsg %chanserv% :REGISTER %testchan%3 test2
:cl1 privmsg %chanserv% :ALLOWREGISTER #*tch*
:cl1 join %testchan%3
:cl1 privmsg %opserv% :ADDBAD %testchan%3
:cl1 privmsg %chanserv% :REGISTER %testchan%3 test2
:cl1 privmsg %opserv% :CHANINFO %testchan%3
:cl1 privmsg %chanserv% :%testchan%1 MOVE %testchan%3
:cl1 join %testchan%3
:cl1 privmsg %opserv% :DELBAD %testchan%3
:cl1 privmsg %opserv% :ADDBAD %testchan%4
:cl1 privmsg %chanserv% :REGISTER %testchan%4 test2
:cl1 privmsg %chanserv% :%testchan%1 MOVE %testchan%4
:cl1 privmsg %opserv% :DELBAD %testchan%4
:cl1 privmsg %chanserv% :REGISTER %testchan%3 test2
:cl1 privmsg %chanserv% :ALLOWREGISTER #pizza
:cl2 wait cl1
:cl2 privmsg %chanserv% :%testchan%3 OPCHAN
:cl1 wait cl2
:cl1 privmsg %chanserv% :%testchan%3 CSUSPEND 1m H8!
:cl2 wait cl1
:cl2 privmsg %chanserv% :%testchan%3 UNREGISTER 1234a2ec
:cl2 privmsg %chanserv% :%testchan%3 OPCHAN
:cl2 privmsg %chanserv% :%testchan%1 UNREGISTER
:cl1 wait cl2
:cl1 privmsg %chanserv% :%testchan%3 CUNSUSPEND
:cl2 wait cl1
:cl2 privmsg %chanserv% :%testchan%3 UNREGISTER
:cl2 privmsg %chanserv% :%testchan%3 OPCHAN
:cl2 privmsg %chanserv% :%testchan%3 UNREGISTER 1234a2ec
:cl1 join %testchan%4
:cl1 privmsg %chanserv% :%testchan%4 UNREGISTER
:cl1 privmsg %chanserv% :%testchan%2 MOVE %testchan%4
:cl1 privmsg %chanserv% :%testchan%4 MERGE %testchan%1
:cl1 privmsg %chanserv% :%testchan%1 OPCHAN
:cl1 privmsg %chanserv% :%testchan%1 CLVL test2 bogus
:cl1 privmsg %chanserv% :%testchan%1 CLVL test2 COOWNER
:cl1 privmsg %chanserv% :%testchan%1 DELUSER COOWNER test2
:cl1 privmsg %chanserv% :%testchan%1 MDELOP *
:cl1 privmsg %chanserv% :%testchan%1 TRIM BANS 1w
:cl1 privmsg %chanserv% :%testchan%1 TRIM USERS 1w
:cl1 privmsg %chanserv% :%testchan%1 DOWN
:cl1 privmsg %chanserv% :%testchan%1 UP
:cl1 privmsg %chanserv% :UPALL
:cl1 privmsg %chanserv% :DOWNALL
:cl1 privmsg %chanserv% :%testchan%1 OP test1
:cl1 privmsg %chanserv% :%testchan%1 OP test2
:cl1 privmsg %chanserv% :%testchan%1 DEOP test2
:cl1 privmsg %chanserv% :%testchan%1 VOICE test2
:cl1 privmsg %chanserv% :%testchan%1 DEVOICE test2
:cl1 privmsg %chanserv% :%testchan%1 ADDTIMEDBAN test2 30s WEH8U
:cl1 privmsg %chanserv% :%testchan%1 BANS
:cl1 privmsg %chanserv% :%testchan%1 UNBAN test3
:cl1 privmsg %chanserv% :%testchan%1 DELBAN test2
:cl1 mode %testchan%1 +bbb abcdef!ghijkl@123456789012345678901234567890mnopqr.stuvwx.yz ghijkl!mnopqr@123456789012345678901234567890stuvwx.yzabcd.ef mnopqr!stuvwx@123456789012345678901234567890yzabcd.efghij.kl
:cl1 mode %testchan%1 +bbb stuvwx!yzabcd@123456789012345678901234567890efghij.klmnop.qr yzabcd!efghij@123456789012345678901234567890klmnop.qrstuv.wx efghij!klmnop@123456789012345678901234567890qrstuv.wxyzab.cd
:cl1 mode %testchan%1 +bbb klmnop!qrstuv@123456789012345678901234567890wxyzab.cdefgh.ij qrstuv!wxyzab@123456789012345678901234567890cdefgh.ijklmn.op wxyzab!cdefgh@123456789012345678901234567890ijklmn.opqrst.uv
:cl1 privmsg %chanserv% :%testchan%1 ADDTIMEDBAN a!b@c.om 15s
:cl1 privmsg %chanserv% :%testchan%1 UNBANALL
:cl1 privmsg %chanserv% :%testchan%1 OPEN
:cl1 privmsg %chanserv% :%testchan%1 ACCESS test2
:cl1 privmsg %chanserv% :%testchan%1 ACCESS test1
:cl1 privmsg %chanserv% :%testchan%1 USERS
:cl1 privmsg %chanserv% :%testchan%1 CSUSPEND 1w WEH8URCHAN
:cl1 privmsg %chanserv% :%testchan%1 INFO
:cl1 privmsg %chanserv% :%testchan%1 CUNSUSPEND
:cl1 privmsg %chanserv% :%testchan%1 PEEK
:cl1 privmsg %chanserv% :%testchan%1 SETINFO Wraa!
:cl1 privmsg %chanserv% :%testchan%1 ADDUSER MASTER test2
:cl2 wait cl1
:cl2 privmsg %chanserv% :%testchan%1 SETINFO Arrr!
:cl1 privmsg %chanserv% :%testchan%1 WIPEINFO test2
:cl1 privmsg %chanserv% :%testchan%1 SEEN test2
:cl2 privmsg %chanserv% :%testchan%1 NAMES
:cl1 privmsg %chanserv% :%testchan%1 EVENTS
:cl1 privmsg %chanserv% :%testchan%1 SAY Hi
:cl1 privmsg %chanserv% :%testchan%1 EMOTE burps.
:cl1 privmsg %chanserv% :CSEARCH PRINT LIMIT 20
:cl1 privmsg %chanserv% :UNVISITED
:cl1 privmsg %chanserv% :%testchan%1 SET DEFAULTTOPIC foo bar baz
:cl1 privmsg %chanserv% :%testchan%1 SET TOPICMASK foo * baz
:cl1 privmsg %chanserv% :%testchan%1 SET ENFTOPIC 5
:cl1 privmsg %chanserv% :%testchan%1 SET GREETING Hello non-user!
:cl1 privmsg %chanserv% :%testchan%1 SET USERGREETING Hello user!
:cl1 privmsg %chanserv% :%testchan%1 SET PUBCMD 6
:cl1 privmsg %chanserv% :%testchan%1 SET PROTECT 0
:cl1 privmsg %chanserv% :%testchan%1 SET TOYS 0
:cl1 privmsg %chanserv% :%testchan%1 SET SETTERS 2
:cl1 privmsg %chanserv% :%testchan%1 SET TOPICREFRESH 1
:cl1 privmsg %chanserv% :%testchan%1 SET USERINFO ON
:cl1 privmsg %chanserv% :%testchan%1 SET DYNLIMIT ON
:cl1 privmsg %chanserv% :%testchan%1 SET TOPICSNARF OFF
:cl1 privmsg %chanserv% :%testchan%1 SET PEONINVITE OFF
:cl1 privmsg %chanserv% :%testchan%1 SET NODELETE ON
:cl1 privmsg %chanserv% :%testchan%1 SET DYNLIMIT OFF
:cl1 privmsg %chanserv% :%testchan%1 SET MODES +nt
:cl1 raw :MODE %testchan%1 +bb abc!def@ghi.com foo!bar@baz.com
:cl1 raw :MODE %testchan%1 -plkb 500 bah foo!bar@baz.com
:cl1 raw :MODE %testchan%1 +plkntDrcC 500 bah
:cl1 raw :CLEARMODE %testchan%1
:cl1 raw :OPMODE %testchan%1 +oo %chanserv% test1
:cl1 raw :GLINE +foo@example.com * 3600 :We don't like Examplians.
:cl1 raw :GLINE -foo@example.com * 3600 :We like you again
:cl1 privmsg %chanserv% :%testchan%1 UNREGISTER
:cl1 privmsg %chanserv% :%testchan%1 TOPIC blah blah blah
:cl1 privmsg %chanserv% :%testchan%1 DEOP %chanserv%
:cl1 raw :KICK %testchan%1 test2
:cl1 raw :TOPIC %testchan%1 :Topic set by test1
:cl1 privmsg %testchan%1 :goodbye

# Test raw protocol functionality
:cl1 raw :STATS u %srvx%
:cl1 raw :STATS c %srvx%
:cl1 raw :VERSION %srvx%
:cl1 raw :ADMIN %srvx%
:cl1 raw :WHOIS %nickserv% %nickserv%
:cl1 join 0
:cl1 raw :AWAY :doing stuff
:cl1 raw :AWAY
:cl1 raw :MODE test1 +iwsdh
:cl1 raw :KILL test3 :die, foo
:cl1 raw :MODE test1 -oiwsdh

# Test gline functions
:cl1 raw :OPER %opernick% %operpass%
:cl1 privmsg %opserv% :gline a@b.com 1h Test gline 1
:cl1 privmsg %opserv% :gline b@c.com 1m Test gline 2
:cl1 privmsg %opserv% :gline b@c.com 1h Test gline 2 (updated)
:cl1 privmsg %opserv% :gline a@a.com 10 Very short gline
:cl1 privmsg %opserv% :refreshg %srv1name%
:cl1 privmsg %opserv% :refreshg
:cl1 privmsg %opserv% :stats glines
:cl1 privmsg %opserv% :gtrace print mask *@* limit 5 issuer test1 reason *
:cl1 privmsg %opserv% :gtrace count mask *@* limit 5 issuer test1 reason *
:cl1 privmsg %opserv% :gtrace ungline mask *@b.com
:cl1 privmsg %opserv% :gtrace break mask *@b.com
:cl1 privmsg %opserv% :trace print ip 66.0.0.0/8 mask *!*@* limit 5
:cl1 privmsg %opserv% :trace print ip 66.*
:cl1 mode %testchan%1 +b abc!def@ghi.com
:cl1 privmsg %opserv% :%testchan%1 BAN def
:cl1 privmsg %opserv% :%testchan%1 BAN *!*@def.ghi.com

# Test modcmd functions
:cl1 privmsg %chanserv% :%testchan%1
:cl1 privmsg %opserv% :TIMECMD BIND %opserv% gumbo *modcmd.bind %opserv% $1- $$
:cl1 privmsg %opserv% :HELP gumbo
:cl1 privmsg %opserv% :gumbo gumbo gumbo
:cl1 privmsg %opserv% :MODCMD gumbo FLAGS gumbo
:cl1 privmsg %opserv% :MODCMD gumbo FLAGS +gumbo
:cl1 privmsg %opserv% :MODCMD gumbo FLAGS +disabled,-oper CHANNEL_LEVEL none
:cl1 privmsg %opserv% :MODCMD gumbo OPER_LEVEL 1001
:cl1 privmsg %opserv% :MODCMD gumbo ACCOUNT_FLAGS +g WEIGHT 0
:cl1 privmsg %opserv% :MODCMD gumbo bogus options
:cl1 privmsg %opserv% :UNBIND %opserv% gumbo
:cl1 privmsg %opserv% :TIMECMD BIND %opserv% gumbo %opserv%.bind %opserv% $1-
:cl1 privmsg %opserv% :UNBIND %opserv% gumbo
:cl1 privmsg %opserv% :STATS
:cl1 privmsg %opserv% :STATS MODULES
:cl1 privmsg %opserv% :STATS MODULES MODCMD
:cl1 privmsg %opserv% :STATS SERVICES
:cl1 privmsg %opserv% :STATS SERVICES %opserv%
:cl1 privmsg %opserv% :READHELP OpServ
:cl1 privmsg %opserv% :SHOWCOMMANDS
:cl1 privmsg %opserv% :HELPFILES %opserv%
:cl1 privmsg %chanserv% :COMMAND REGISTER

# Test HelpServ functions
connect cl3 test3 test3 %srv1% :Test Bot 3
:cl1 privmsg %opserv% :HELPSERV REGISTER %helpserv% %testchan%1 test1
:cl1 privmsg %helpserv% :huh?
:cl1 privmsg %helpserv% :ADDHELPER test2
:cl1 privmsg %helpserv% :CLVL test2 pizzaboy
:cl1 privmsg %helpserv% :DELUSER test2
:cl1 privmsg %helpserv% :DELUSER testy
:cl1 privmsg %helpserv% :SET PAGETARGET %testchan%1
:cl1 privmsg %helpserv% :SET PAGETYPE NOTICE
:cl1 privmsg %helpserv% :SET ALERTPAGETARGET %testchan%1
:cl1 privmsg %helpserv% :SET ALERTPAGETYPE PRIVMSG
:cl1 privmsg %helpserv% :SET STATUSPAGETARGET %testchan%1
:cl1 privmsg %helpserv% :SET STATUSPAGETYPE ONOTICE
:cl1 privmsg %helpserv% :SET GREETING Hello Earthling!  Please talk to me!
:cl1 privmsg %helpserv% :SET REQOPENED Your request has been accepted!
:cl1 privmsg %helpserv% :SET REQASSIGNED Your request has been assigned to a helper!
:cl1 privmsg %helpserv% :SET REQCLOSED Goodbye and leave us alone next time!
:cl1 privmsg %helpserv% :SET IDLEDELAY 5m
:cl1 privmsg %helpserv% :SET WHINEDELAY 3m
:cl1 privmsg %helpserv% :SET WHINEINTERVAL 3m
:cl1 privmsg %helpserv% :SET EMPTYINTERVAL 3m
:cl1 privmsg %helpserv% :SET STALEDELAY 5m
:cl1 privmsg %helpserv% :SET REQPERSIST PART
:cl1 privmsg %helpserv% :SET HELPERPERSIST CLOSE
:cl1 privmsg %helpserv% :SET NOTIFICATION ACCOUNTCHANGES
:cl1 privmsg %helpserv% :SET REQMAXLEN 5
:cl1 privmsg %helpserv% :SET IDWRAP 10
:cl1 privmsg %helpserv% :SET REQONJOIN ON
:cl1 privmsg %helpserv% :SET AUTOVOICE ON
:cl1 privmsg %helpserv% :SET AUTODEVOICE ON
:cl1 privmsg %helpserv% :SET
:cl1 privmsg %helpserv% :LIST ALL
:cl3 wait cl1
:cl3 join %testchan%1
:cl3 privmsg %helpserv% :eye kant auth 2 my acount test2 plz 2 help!
:cl1 wait cl3
:cl1 privmsg %helpserv% :LIST
:cl1 privmsg %helpserv% :LIST ASSIGNED
:cl1 privmsg %helpserv% :STATS
:cl1 privmsg %helpserv% :STATS test1
:cl1 privmsg %helpserv% :NEXT
:cl1 privmsg %helpserv% :NEXT
:cl1 privmsg %helpserv% :PICKUP test3
:cl1 privmsg %helpserv% :LIST ASSIGNED
:cl1 privmsg %helpserv% :LIST UNASSIGNED
:cl1 privmsg %helpserv% :LIST ALL
:cl1 privmsg %helpserv% :LIST PIZZA
:cl1 privmsg %nickserv% :ALLOWAUTH test3 test5
:cl1 privmsg %nickserv% :ALLOWAUTH test3 test2
:cl1 privmsg %nickserv% :ALLOWAUTH test3
:cl1 privmsg %nickserv% :ALLOWAUTH test3 test2
:cl3 wait cl1
:cl3 nick test4
:cl3 privmsg %nickserv%@%srvx% :AUTH test2 tested
:cl3 nick test3
:cl1 wait cl3
:cl1 privmsg %nickserv% :ALLOWAUTH test3 test2
:cl1 privmsg %helpserv% :REASSIGN test3 test1
:cl3 wait cl1
:cl3 privmsg %nickserv%@%srvx% :AUTH test2 testest
:cl3 privmsg %helpserv% :THX IT WORX NOW!!
:cl1 wait cl3
:cl1 privmsg %helpserv% :LIST ME
:cl1 privmsg %helpserv% :ADDNOTE george this guy is a tool
:cl1 privmsg %helpserv% :ADDNOTE test2 this should be the first note that works
:cl1 privmsg %helpserv% :ADDNOTE *test2 this guy is a tool
:cl1 privmsg %helpserv% :CLOSE 2
:cl1 privmsg %helpserv% :SHOW 1
:cl1 privmsg %helpserv% :CLOSE test3
:cl1 privmsg %opserv% :RECONNECT
:cl1 sleep 20
:cl1 privmsg %helpserv% :HELP
:cl1 privmsg %helpserv% :HELP COMMANDS
:cl1 privmsg %helpserv% :HELP BOTS
:cl1 privmsg %helpserv% :BOTS
:cl1 privmsg %nickserv% :SET BOGUS
:cl1 privmsg %nickserv% :SET STYLE DEF
:cl1 privmsg %helpserv% :HELPERS
:cl1 privmsg %nickserv% :SET STYLE ZOOT
:cl1 privmsg %helpserv% :HELPERS
:cl1 privmsg %helpserv% :VERSION CVS
:cl1 privmsg %helpserv% :PAGE and i-----i'm calling all you angels
:cl1 privmsg %helpserv% :STATSREPORT
:cl1 part %testchan%1
:cl1 privmsg %opserv% :HELPSERV
:cl1 privmsg %opserv% :HELPSERV BOGUS
:cl1 privmsg %opserv% :HELPSERV PICKUP
:cl1 privmsg %opserv% :HELPSERV READHELP
:cl1 privmsg %opserv% :HELPSERV BOTS
:cl1 privmsg %opserv% :HELPSERV STATS %helpserv%
:cl1 privmsg %opserv% :HELPSERV STATS %helpserv% test1
:cl1 privmsg %opserv% :HELPSERV MOVE %helpserv% %helpserv2%
:cl1 privmsg %opserv% :HELPSERV UNREGISTER %helpserv2%

# Test NickServ functions
:cl1 privmsg %nickserv% :STATUS
:cl1 privmsg %nickserv% :VERSION
:cl1 privmsg %nickserv% :HELP COMMANDS
:cl1 privmsg %nickserv% :ADDMASK
:cl1 privmsg %nickserv% :ADDMASK *!**foo@**.bar.com
:cl1 privmsg %nickserv% :ADDMASK **foo@**.bar.com
:cl1 privmsg %nickserv% :OADDMASK test1 *!**foo@**.bar.com
:cl1 privmsg %nickserv% :ODELMASK test1 *!**foo@**.bar.com
:cl1 privmsg %nickserv% :DELMASK **foo@**.bar.com
:cl1 privmsg %nickserv% :DELMASK *@*.%domain%
:cl1 privmsg %nickserv% :SEARCH PRINT HOSTMASK
:cl1 privmsg %nickserv% :SEARCH PRINT HOSTMASK EXACT *foo@*.bar.com LIMIT 5 REGISTERED >=1m
# cannot test with email since it breaks profiling.. argh
:cl3 privmsg %nickserv%@%srvx% :REGISTER test3 bleh
:cl1 wait cl3
:cl1 privmsg %nickserv% :OUNREGISTER *bleh
:cl1 privmsg %nickserv%@%srvx% :OREGISTER test4 bleh *@* test3
:cl1 privmsg %nickserv%@%srvx% :OREGISTER test4 bleh test3@bar
:cl1 privmsg %nickserv% :ACCOUNTINFO test3
:cl1 privmsg %nickserv% :ACCOUNTINFO test3bcd
:cl1 privmsg %nickserv% :USERINFO test3
:cl1 privmsg %nickserv% :NICKINFO test3
:cl1 privmsg %nickserv% :OSET test3
:cl1 privmsg %nickserv% :OSET jobaba
:cl1 privmsg %nickserv% :OSET test3 BOGUS
:cl1 privmsg %nickserv% :OSET test3 FLAGS +f
:cl1 privmsg %nickserv% :RENAME test4 test3
:cl3 wait cl1
:cl3 privmsg %nickserv%@%srvx% :REGISTER test3 bleh
:cl3 privmsg %nickserv%@%srvx% :AUTH bleh
:cl1 wait cl3
:cl1 privmsg %nickserv% :ALLOWAUTH test3 test2
:cl3 wait cl1
:cl3 nick test4
:cl3 privmsg %nickserv% :REGNICK
:cl3 nick test3
:cl3 privmsg %nickserv%@%srvx% :REGISTER test3 bleh
:cl3 privmsg %nickserv%@%srvx% :AUTH bleh
:cl3 privmsg %nickserv%@%srvx% :PASS bleh blargh
:cl3 privmsg %nickserv%@%srvx% :ADDMASK *@foo.%domain%
:cl3 privmsg %nickserv%@%srvx% :DELMASK *@foo.%domain%
:cl3 privmsg %nickserv%@%srvx% :SET
:cl3 privmsg %nickserv%@%srvx% :SET MAXLOGINS 1
:cl3 privmsg %nickserv%@%srvx% :RECLAIM test3
:cl3 privmsg %nickserv%@%srvx% :UNREGNICK test3
:cl3 privmsg %nickserv%@%srvx% :UNREGISTER bleach
:cl1 wait cl3
:cl3 quit
:cl1 sleep 5
:cl1 privmsg %nickserv% :RENAME *test4 test3
:cl1 privmsg %nickserv% :OSET *test3 INFO hi hi hi!
:cl1 privmsg %nickserv% :OSET *test3 WIDTH 1
:cl1 privmsg %nickserv% :OSET *test3 WIDTH 80
:cl1 privmsg %nickserv% :OSET *test3 WIDTH 1000
:cl1 privmsg %nickserv% :OSET *test3 TABLEWIDTH 1
:cl1 privmsg %nickserv% :OSET *test3 TABLEWIDTH 80
:cl1 privmsg %nickserv% :OSET *test3 TABLEWIDTH 1000
:cl1 privmsg %nickserv% :OSET *test3 COLOR OFF
:cl1 privmsg %nickserv% :OSET *test3 COLOR ON
:cl1 privmsg %nickserv% :OSET *test3 COLOR TV
:cl1 privmsg %nickserv% :OSET *test3 PRIVMSG ON
:cl1 privmsg %nickserv% :OSET *test3 PRIVMSG OFF
:cl1 privmsg %nickserv% :OSET *test3 PRIVMSG IGNORED
:cl1 privmsg %nickserv% :OSET *test3 ANNOUNCEMENTS ON
:cl1 privmsg %nickserv% :OSET *test3 ANNOUNCEMENTS OFF
:cl1 privmsg %nickserv% :OSET *test3 ANNOUNCEMENTS ?
:cl1 privmsg %nickserv% :OSET *test3 ANNOUNCEMENTS ARE NOT SPAM
:cl1 privmsg %nickserv% :OSET *test3 PASSWORD whocares?
:cl1 privmsg %nickserv% :ACCOUNTINFO *test3
:cl1 privmsg %nickserv% :OSET *test3 INFO *
:cl1 privmsg %nickserv% :OREGISTER test4 bleh *@*
:cl1 privmsg %nickserv% :OREGISTER test4@bogus bleh *@*
:cl1 privmsg %nickserv% :OREGNICK *test3 test3a
:cl1 privmsg %nickserv% :OREGNICK *test3 test3b
:cl1 privmsg %nickserv% :OREGNICK *test3 test3c
:cl1 privmsg %nickserv% :OUNREGNICK test3c
:cl1 privmsg %nickserv% :OUNREGNICK test3b
:cl1 privmsg %nickserv% :OUNREGNICK test3a
:cl1 privmsg %chanserv% :REGISTER %testchan%2 *test2
:cl1 privmsg %chanserv% :REGISTER %testchan%3 *test3
:cl1 privmsg %chanserv% :%testchan%2 ADDUSER COOWNER *test3
:cl1 privmsg %chanserv% :%testchan%3 ADDUSER COOWNER *test2
:cl1 privmsg %chanserv% :%testchan%1 ADDUSER COOWNER *test3
:cl1 privmsg %chanserv% :%testchan%1 ADDUSER COOWNER *test2
:cl1 privmsg %nickserv% :MERGE *test3 *test2
:cl1 privmsg %nickserv% :SET STYLE DEF
:cl1 privmsg %chanserv% :%testchan%1 USERS
:cl1 privmsg %chanserv% :%testchan%2 USERS
:cl1 privmsg %chanserv% :%testchan%3 USERS
:cl1 privmsg %nickserv% :ACCOUNTINFO *test2
:cl1 privmsg %nickserv% :OSET *test2 MAXLOGINS 100
:cl1 privmsg %nickserv% :OSET *test2 MAXLOGINS 1
:cl1 privmsg %nickserv% :OSET *test2 LEVEL 999
:cl1 privmsg %nickserv% :OSET *test2 LEVEL 998
connect cl3 test3 test3 %srv1% :Test Bot 3
:cl1 sleep 6
:cl3 wait cl1
:cl3 privmsg %nickserv%@%srvx% :AUTH test2 testest
:cl3 privmsg %nickserv% :VACATION
:cl2 wait cl3
:cl2 privmsg %nickserv% :GHOST test3
:cl3 sleep 3
:cl3 quit

# Test OpServ functions
:cl1 privmsg %opserv% :ACCESS
:cl1 privmsg %opserv% :ACCESS *
:cl1 privmsg %opserv% :CHANINFO %testchan%1
:cl1 privmsg %opserv% :WHOIS test1
:cl1 privmsg %opserv% :INVITEME
:cl1 privmsg %opserv% :JOIN %testchan%1
:cl1 privmsg %opserv% :PART %testchan%1
:cl1 privmsg %opserv% :STATS BAD
:cl1 privmsg %opserv% :STATS GLINES
:cl1 privmsg %opserv% :STATS LINKS
:cl1 privmsg %opserv% :STATS MAX
:cl1 privmsg %opserv% :STATS NETWORK
:cl1 privmsg %opserv% :STATS NETWORK2
:cl1 privmsg %opserv% :STATS RESERVED
:cl1 privmsg %opserv% :STATS TRUSTED
:cl1 privmsg %opserv% :STATS UPLINK
:cl1 privmsg %opserv% :STATS UPTIME
:cl1 privmsg %opserv% :STATS ALERTS
:cl1 privmsg %opserv% :STATS GAGS
:cl1 privmsg %opserv% :STATS TIMEQ
:cl1 privmsg %opserv% :STATS WARN
:cl1 privmsg %opserv% :VERSION
:cl1 privmsg %opserv% :HELP COMMANDS
:cl1 privmsg %opserv% :HELP USER
:cl1 privmsg %opserv% :TRACE DOMAINS DEPTH 2
:cl1 privmsg %opserv% :TRACE COUNT LIMIT 3
:cl1 privmsg %opserv% :TRACE HULA-HOOP LIMIT 3
:cl1 privmsg %opserv% :CSEARCH PRINT NAME * TOPIC * USERS <3 TIMESTAMP >0 LIMIT 5
:cl1 privmsg %opserv% :CSEARCH COUNT NAME * TOPIC * USERS <3 TIMESTAMP >0 LIMIT 5
:cl1 privmsg %opserv% :WARN %testchan%4 quiche eaters live here
:cl1 privmsg %opserv% :STATS WARN
:cl1 join %testchan%4
:cl1 privmsg %opserv% :UNWARN %testchan%4
:cl1 mode %testchan%4 +bbbsnt a!b@c.com b!c@a.org c!a.b.net
:cl1 privmsg %opserv% :CLEARBANS %testchan%4
:cl1 privmsg %opserv% :CLEARMODES %testchan%4
:cl1 privmsg %opserv% :DEOP %testchan%4 test1
:cl1 privmsg %opserv% :OP %testchan%4 test1
:cl1 privmsg %opserv% :DEOPALL %testchan%4
:cl1 privmsg %opserv% :VOICEALL %testchan%4
:cl1 privmsg %opserv% :OPALL %testchan%4
:cl1 privmsg %opserv% :JUPE crap.tacular.net 4095 Craptacular Jupe Server
:cl1 privmsg %opserv% :UNJUPE crap.tacular.net
:cl1 privmsg %opserv% :JUMP clan-dk
:cl1 privmsg %opserv% :GLINE pizza 1y Pizza is not allowed on this network
:cl1 privmsg %opserv% :GLINE *@* 1w GO AWAY I HATE THE WORLD
:cl1 privmsg %opserv% :GLINE pizza@thehut.com 0 Fat-laden freak
:cl1 privmsg %opserv% :GLINE foo@bar.com 1m Testing G-line removal
:cl1 privmsg %opserv% :UNGLINE foo@bar.com 1m Testing G-line removal
:cl1 privmsg %opserv% :UNGLINE foo@bar.com 1m Testing G-line removal
:cl1 privmsg %opserv% :REFRESHG pizza.thehut.com
:cl1 privmsg %opserv% :GSYNC %srv1name%.illegal
:cl1 privmsg %opserv% :GSYNC
:cl1 privmsg %opserv% :WHOIS test1
:cl1 privmsg %opserv% :JOIN pizza.thehut.com
:cl1 privmsg %opserv% :JOIN %testchan%4
:cl1 privmsg %opserv% :JOIN %testchan%4
:cl1 privmsg %opserv% :KICK %testchan%4 test1
:cl1 join %testchan%4
:cl1 privmsg %opserv% :KICKALL %testchan%4
:cl1 join %testchan%4
:cl1 privmsg %opserv% :KICKBAN %testchan%4 test1
:cl1 privmsg %opserv% :PART %testchan%4 hahah u r banned
:cl1 join %testchan%4
:cl1 privmsg %opserv% :MODE %testchan%4 +snti
:cl1 privmsg %opserv% :NICKBAN %testchan%4 test1
:cl1 privmsg %opserv% :UNBAN %testchan%4 *!*@*.%domain%
:cl1 privmsg %opserv% :KICKBANALL %testchan%4
:cl1 part %testchan%4
:cl1 privmsg %opserv% :COLLIDE test3 foo bar.com nick jupe
:cl1 privmsg %opserv% :UNRESERVE test3
:cl1 privmsg %opserv% :RESERVE test3 foo bar.com nick jupe 2
:cl1 privmsg %opserv% :UNRESERVE test3
:cl1 privmsg %opserv% :ADDBAD %testchan%4abc
:cl1 privmsg %opserv% :ADDBAD %testchan%4
:cl1 privmsg %opserv% :ADDBAD %testchan%4abc EXCEPT
:cl1 privmsg %opserv% :ADDBAD %testchan%4abc EXCEPT %testchan%4ab
:cl1 privmsg %opserv% :ADDEXEMPT %testchan%4ab
:cl1 privmsg %opserv% :DELEXEMPT %testchan%4ab
:cl1 privmsg %opserv% :ADDTRUST 1.2.3.4 0 1w We like incrementing numbers
:cl1 privmsg %opserv% :ADDTRUST foo@1.2.3.4 0 1w We like incrementing numbers
:cl1 privmsg %opserv% :ADDTRUST 1.2.3.4 0 1w We like incrementing numbers
:cl1 privmsg %opserv% :DELTRUST 1.2.3.4
:cl1 privmsg %opserv% :CLONE ADD test3 joe.bar.com nick jupe 3
:cl1 privmsg %opserv% :CLONE ADD test3 joe@bar.com nick jupe 3
:cl1 privmsg %opserv% :CLONE REMOVE gobbledygook
:cl1 privmsg %opserv% :CLONE REMOVE %chanserv%
:cl1 privmsg %opserv% :CLONE bogus test3
:cl1 privmsg %opserv% :CLONE JOIN test3 %testchan%1
:cl1 privmsg %opserv% :CLONE OP test3 %testchan%1
:cl1 privmsg %opserv% :CLONE SAY test3 %testchan%1
:cl1 privmsg %opserv% :CLONE SAY test3 %testchan%1 HAHA H4X
:cl1 privmsg %opserv% :CLONE JOIN test3 %testchan%1abc
:cl1 privmsg %opserv% :CLONE PART test3 %testchan%1
:cl1 privmsg %opserv% :CLONE REMOVE test3
:cl1 privmsg %opserv% :GAG test3!*@*.%domain% 1w Clones sux
connect cl3 test3 test3 %srv2% :Test Bot 3
:cl1 wait cl3
:cl1 privmsg %opserv% :ADDALERT test3 kill NICK test3
:cl1 privmsg %opserv% :DELALERT test3 kill NICK test3
:cl3 privmsg %nickserv% :HELP
:cl3 nick test4
:cl3 privmsg %nickserv% :HELP
:cl3 nick test3
:cl3 privmsg %nickserv% :HELP
:cl1 privmsg %opserv% :UNGAG test3!*@*.%domain%
:cl1 privmsg %opserv% :SET server/max_users 128
:cl1 privmsg %opserv% :SETTIME *

# Test MemoServ functions
:cl1 privmsg %memoserv% :SEND gobble,dy HELLO?
:cl1 privmsg %memoserv% :SEND test2 HELLO?
:cl1 privmsg %memoserv% :SET NOTIFY ON
:cl1 privmsg %memoserv% :SET AUTHNOTIFY ON
:cl2 wait cl1
:cl2 privmsg %memoserv% :SET NOTIFY OFF
:cl2 privmsg %memoserv% :SET AUTHNOTIFY OFF
:cl2 privmsg %memoserv% :LIST
:cl2 privmsg %memoserv% :SEND test1 HELLO!
:cl2 privmsg %memoserv% :DELETE 0
:cl1 wait cl2
:cl1 privmsg %memoserv% :SET PRIVATE ON
:cl2 wait cl1
:cl2 privmsg %memoserv% :SEND test1 DO YOU STILL LIKE ME?
:cl1 wait cl2
:cl1 privmsg %chanserv% :%testchan%1 DELUSER test2
:cl1 privmsg %nickserv% :RENAME test2 testy
:cl2 wait cl1
:cl2 privmsg %memoserv% :SEND test1 DO YOU STILL LIKE ME?
:cl1 privmsg %memoserv% :LIST
:cl1 privmsg %memoserv% :READ 1
:cl1 privmsg %memoserv% :READ 10
:cl1 privmsg %memoserv% :DELETE 10
:cl1 privmsg %memoserv% :DELETE ALL
:cl1 privmsg %memoserv% :DELETE ALL CONFIRM
:cl1 privmsg %memoserv% :EXPIRE
:cl1 privmsg %memoserv% :EXPIRY
:cl1 privmsg %memoserv% :VERSION
:cl1 privmsg %memoserv% :STATUS

# Test ServerSpy functions
:cl1 privmsg %opserv% :DISCONNECT
:cl1 privmsg %opserv% :DISCONNECT
:cl1 privmsg %opserv% :STATS SERVERSPY
:cl1 privmsg %opserv% :CONNECT
:cl1 privmsg %opserv% :CONNECT
:cl1 privmsg %opserv% :DELMOD hl bogus
:cl1 privmsg %opserv% :DELMOD hl cstrike
:cl1 privmsg %opserv% :DELMOD bogus cstrike
:cl1 privmsg %opserv% :DELGAME hl
:cl1 privmsg %opserv% :DELGAME hl
:cl1 privmsg %opserv% :ADDGAME hl Half Life
:cl1 privmsg %opserv% :ADDGAME hl Half Life
:cl1 privmsg %opserv% :ADDMOD hl cstrike Counter-Strike
:cl1 privmsg %opserv% :ADDMOD hl cstrike Counter-Strike
:cl1 privmsg %opserv% :ADDMOD bogus cstrike Counter-Strike
:cl1 privmsg %chanserv% :HELP SERVERSPY
:cl1 privmsg %chanserv% :SERVERSPY GAME hl
:cl1 privmsg %chanserv% :SERVERSPY NAME Jose
:cl1 privmsg %chanserv% :SERVERSPY NAME Jose GAME bogus
:cl1 privmsg %chanserv% :SERVERSPY NAME Jose GAME hl MOD bogus
:cl1 privmsg %chanserv% :SERVERSPY NAME Jose GAME hl MOD cstrike
:cl1 privmsg %chanserv% :SERVERSPY NAME *p* GAME hl MOD cstrike
:cl1 privmsg %chanserv% :SERVERSPY SERVER *?p* GAME hl MOD cstrike
:cl1 privmsg %chanserv% :%testchan%1 SET GAME
:cl1 privmsg %chanserv% :%testchan%1 SET GAME bogus
:cl1 privmsg %chanserv% :%testchan%1 SET GAME hl
:cl1 privmsg %chanserv% :%testchan%1 SET GAME
:cl1 privmsg %chanserv% :%testchan%1 SET MOD
:cl1 privmsg %chanserv% :%testchan%1 SET MOD bogus
:cl1 privmsg %chanserv% :%testchan%1 SET MOD cstrike
:cl1 privmsg %chanserv% :%testchan%1 SET MOD
:cl1 privmsg %chanserv% :%testchan%1 SET CLANTAG [D]
:cl1 privmsg %chanserv% :%testchan%1 SET CLANTAG [D*
:cl1 privmsg %chanserv% :%testchan%1 SET CLANTAG
:cl1 privmsg %chanserv% :%testchan%1 SET SERVERTAG [D]
:cl1 privmsg %chanserv% :%testchan%1 SET SERVERTAG [D*
:cl1 privmsg %chanserv% :%testchan%1 SET SERVERTAG
:cl1 privmsg %chanserv% :%testchan%1 SERVERSPY NAME *p*
:cl1 privmsg %chanserv% :%testchan%1 LOCATECLAN
:cl1 privmsg %chanserv% :%testchan%1 LOCATESERVER
:cl1 privmsg %opserv% :STATS SERVERSPY

# Test proxy checker code
:cl1 privmsg %opserv% :HOSTSCAN 62.255.216.72
:cl1 sleep 10
:cl1 privmsg %opserv% :CLEARHOST 62.255.216.72

# Clean up test channel
:cl1 privmsg %chanserv% :%testchan%1 SET NODELETE OFF
:cl1 privmsg %chanserv% :%testchan%1 UNREGISTER

# exit all clients
:cl2 wait cl1
:cl2 privmsg %nickserv%@%srvx% :UNREGISTER MY SHIZNIT
:cl2 privmsg %nickserv%@%srvx% :UNREGISTER testest
:cl1 wait cl2
:cl1 quit
:cl2 quit
:cl3 quit

# THINGS NOT HIT YET:
# announcing user modes +w, +s, +d, +g, +h, +x
# sending bursts with:
#  user list wrapping to a new line
#  voiced users on srvx's side
#  ban list wrapping to a new line (on first ban or on later bans)
#  sending ERROR
#  KILL from a real user
#  sending SVSNICK
#  sending PART with no reason (not just an empty reason)
#  sending raw text
#  calling change_nicklen()
#  receiving numerics 331, 432 from uplink
#  receiving AC from uplink
#  receiving FA from uplink
#  .. or any other fake host support
#  receiving voiced users in burst
#  receiving a burst where remote channel is younger
#  receiving a KILL from uplink
#  receiving a SQUIT from uplink
#  receiving a NOTICE from uplink
#  receiving a GLINE from uplink
#  receiving a MODE <nick> change for: +s, +h, +f
#  receiving a MODE <#channel> change for: +p, -k, -b
#  receiving a ERROR from uplink
#  clearing modes for a channel with modes: +t, +n, +D, +r, +c, +C, +b
#  removing a ban from a channel where an earlier ban doesn't match
#  mod_chanmode() with MC_NOTIFY flag
#  various hostmask generation options
