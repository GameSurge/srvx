define srv1 irc.clan-dk.org:7701
define srv1-name irc0.clan-dk.org
define srv2 irc.clan-dk.org:7711
define srv2-name irc1.clan-dk.org
define nickserv NickServ-Ent

# Connect one client
define cl1-nick D00dm4n
connect cl1 %cl1-nick% d00dm4n %srv1% :Some Dude Man
:cl1 raw :STATS u
:cl1 expect %srv1-name% 242 %cl1-nick% :Server Up
:cl1 expect %srv1-name% 219 u :End of /STATS report
:cl1 raw :WHOIS %nickserv% %nickserv%
:cl1 expect %srv1-name% 311 %cl1-nick% %nickserv%
:cl1 expect %srv1-name% 312 %cl1-nick% %nickserv%
:cl1 expect %srv1-name% 313 %cl1-nick% %nickserv%
:cl1 expect %srv1-name% 318 %cl1-nick% %nickserv% :End of /WHOIS list.
:cl1 raw :WHOIS %nickserv% not-nickserv
:cl1 expect %srv1-name% 401 %cl1-nick% not-nickserv :No such nick
:cl1 raw :PING 1
# TODO: expect
:cl1 raw :PING %cl1-nick% :%srv1-name%
:cl1 expect :PONG %srv1-name% %cl1-nick%

define channel #random-channel
:cl1 join %channel%
:cl1 mode %channel% +ntspimDlkb 12345 foobar foo!bar@baz
:cl1 mode %channel% +bbb bar!baz@bat baz!bat@blah bat!blah@foo
