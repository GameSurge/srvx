define srv1 irc.clan-dk.org:7731
define srv2 irc.clan-dk.org:7731
define nickserv NickServ-Ent@srvx.clan-dk.org
 
connect cl1 D00dm4n d00dm4n %srv1% :Some Dude Man
connect cl2 D00dl4dy d00dl4dy %srv2% :Some Dude Lady
:cl1,cl2 join #test
sync cl1,cl2
:cl1 privmsg #test :We're here and we're bots!
:cl2 expect *cl1 public #test :We're here and we're bots!
:cl2 privmsg #test :And we'll leave soon!
:cl1 privmsg #test :Right, Annette!
:cl1 sleep 10
:cl2 wait cl1
:cl1,cl2 quit
