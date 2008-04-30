#undef gettimeofday
#undef memcpy
#undef memset
#undef strerror

#include "common.h"

#ifdef HAVE_SYS_TIMEB_H
# include <sys/timeb.h>
#endif
#ifdef HAVE_MEMORY_H
# include <memory.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#if !defined(HAVE_GETTIMEOFDAY) && defined(HAVE_FTIME)
int gettimeofday(struct timeval * tv, struct timezone * tz)
{
    if (!tv)
    {
        errno = EFAULT;
        return -1;
    }

    struct timeb tb;

    ftime(&tb); /* FIXME: some versions are void return others int */

    tv->tv_sec  = tb.time;
    tv->tv_usec = ((long)tb.millitm)*1000;

    return 0; (void)tz;
}
#endif

#if !defined(HAVE_GETLOCALTIME_R) && defined(HAVE_LOCALTIME)
struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    memcpy(result, localtime(timep), sizeof(*result));
    return result;
}
#endif

#ifndef HAVE_MEMCPY
void * memcpy(void * dest, void const * src, unsigned long n)
{
/* very slow, your fault for not having memcpy()*/
    unsigned char * td=dest;
    unsigned char * ts=src;
    unsigned long   i;

    if (!td || !ts)
        return NULL;

    for (i=0; i<n; i++)
        td[i] = ts[i];
    return dest;
}
#endif

#ifndef HAVE_MEMSET
/* very slow, deal with it */
void * memset(void * dest, int c, unsigned long n)
{
    unsigned char * temp=dest;
    unsigned long   i;

    if (!temp)
        return NULL;

    for (i=0; i<n; i++)
        temp[i] = (unsigned char)c;
    return dest;
}
#endif

#ifndef HAVE_STRDUP
char * strdup(char const * str)
{
    char * out;

    if (!str)
        return NULL;
    if (!(out = malloc(strlen(str)+1)))
        return NULL;
    strcpy(out,str);
    return out;
}
#endif

#ifndef HAVE_STRERROR
char const * strerror(int errornum)
{
    if (errornum==0)
        return "No error";
#ifdef EPERM
    if (errornum==EPERM)
        return "Operation not permitted";
#endif
#ifdef ENOENT
    if (errornum==ENOENT)
        return "No such file or directory";
#endif
#ifdef ESRCH
    if (errornum==ESRCH)
        return "No such process";
#endif
#ifdef EINTR
    if (errornum==EINTR)
        return "Interrupted system call";
#endif
#ifdef EIO
    if (errornum==EIO)
        return "I/O error";
#endif
#ifdef ENXIO
    if (errornum==EIO)
        return "No such device or address";
#endif
#ifdef EBADF
    if (errornum==EBADF)
        return "Bad file number";
#endif
#ifdef EAGAIN
    if (errornum==EAGAIN)
        return "Try again";
#endif
#ifdef ENOMEM
    if (errornum==ENOMEM)
        return "Out of memory";
#endif
#ifdef EACCES
    if (errornum==EACCES)
        return "Permission denied";
#endif
#ifdef EFAULT
    if (errornum==EFAULT)
        return "Bad address";
#endif
#ifdef EBUSY
    if (errornum==EBUSY)
        return "Device or resource busy";
#endif
#ifdef EEXIST
    if (errornum==EEXIST)
        return "File exists";
#endif
#ifdef EXDEV
    if (errornum==EXDEV)
        return "Cross-device link";
#endif
#ifdef EDEADLK
    if (errornum==EXDEV)
        return "Resource deadlock would occur";
#endif
#ifdef EDEADLOCK
    if (errornum==EDEADLOCK)
        return "Resource deadlock would occur";
#endif
#ifdef ENODEV
    if (errornum==ENODEV)
        return "No such device";
#endif
#ifdef ENOTDIR
    if (errornum==ENOTDIR)
        return "Not a directory";
#endif
#ifdef EISDIR
    if (errornum==EISDIR)
        return "Is a directory";
#endif
#ifdef EINVAL
    if (errornum==EINVAL)
        return "Invalid argument";
#endif
#ifdef ENFILE
    if (errornum==ENFILE)
        return "Too many open files in system";
#endif
#ifdef EMFILE
    if (errornum==EMFILE)
        return "Too many open files";
#endif
#ifdef ENOTTY
    if (errornum==ENOTTY)
        return "Not a typewriter";
#endif
#ifdef ETXTBSY
    if (errornum==ETXTBSY)
        return "Text file busy";
#endif
#ifdef EFBIG
    if (errornum==EFBIG)
        return "File too large";
#endif
#ifdef ENOSPC
    if (errornum==ENOSPC)
        return "No space left on device";
#endif
#ifdef ESPIPE
    if (errornum==ESPIPE)
        return "Illegal seek";
#endif
#ifdef EROFS
    if (errornum==EROFS)
        return "Read-only file system";
#endif
#ifdef EMLINK
    if (errornum==EMLINK)
        return "Too many links";
#endif
#ifdef EPIPE
    if (errornum==EPIPE)
        return "Broken pipe";
#endif
#ifdef EDOM
    if (errornum==EDOM)
        return "Math argument out of domain of func";
#endif
#ifdef ERANGE
    if (errornum==ERANGE)
        return "Math result not representable";
#endif
#ifdef ENAMETOOLONG
    if (errornum==ENAMETOOLONG)
        return "File name too long";
#endif
#ifdef ENOLCK
    if (errornum==ENOLCK)
        return "No record locks avaliable";
#endif
#ifdef ENOSYS
    if (errornum==ENOSYS)
        return "Function not implemented";
#endif
#ifdef ENOTEMPTY
    if (errornum==ENOTEMPTY)
        return "Directory not empty";
#endif
#ifdef ELOOP
    if (errornum==ELOOP)
        return "Too many symbolic links encountered";
#endif
#ifdef EHOSTDOWN
    if (errornum==EHOSTDOWN)
        return "Host is down";
#endif
#ifdef EHOSTUNREACH
    if (errornum==EHOSTUNREACH)
        return "No route to host";
#endif
#ifdef EALREADY
    if (errornum==EALREADY)
        return "Operation already in progress";
#endif
#ifdef EINPROGRESS
    if (errornum==EINPROGRESS)
        return "Operation now in progress";
#endif
#ifdef ESTALE
    if (errornum==ESTALE)
        return "Stale NFS filehandle";
#endif
#ifdef EDQUOT
    if (errornum==EDQUOT)
        return "Quota exceeded";
#endif
#ifdef EWOULDBLOCK
    if (errornum==EWOULDBLOCK)
        return "Operation would block";
#endif
#ifdef ECOMM
    if (errornum==ECOMM)
        return "Communication error on send";
#endif
#ifdef EPROTO
    if (errornum==EPROTO)
        return "Protocol error";
#endif
#ifdef EPROTONOSUPPORT
    if (errornum==EPROTONOSUPPORT)
        return "Protocol not supported";
#endif
#ifdef ESOCKTNOSUPPORT
    if (errornum==ESOCKTNOSUPPORT)
        return "Socket type not supported";
#endif
#ifdef ESOCKTNOSUPPORT
    if (errornum==EOPNOTSUPP)
        return "Operation not supported";
#endif
#ifdef EPFNOSUPPORT
    if (errornum==EPFNOSUPPORT)
        return "Protocol family not supported";
#endif
#ifdef EAFNOSUPPORT
    if (errornum==EAFNOSUPPORT)
        return "Address family not supported by protocol family";
#endif
#ifdef EADDRINUSE
    if (errornum==EADDRINUSE)
        return "Address already in use";
#endif
#ifdef EADDRNOTAVAIL
    if (errornum==EADDRNOTAVAIL)
        return "Cannot assign requested address";
#endif
#ifdef ENETDOWN
    if (errornum==ENETDOWN)
        return "Network is down";
#endif
#ifdef ENETUNREACH
    if (errornum==ENETUNREACH)
        return "Network is unreachable";
#endif
#ifdef ENETRESET
    if (errornum==ENETRESET)
        return "Network dropped connection on reset";
#endif
#ifdef ECONNABORTED
    if (errornum==ECONNABORTED)
        return "Software caused connection abort";
#endif
#ifdef ECONNRESET
    if (errornum==ECONNRESET)
        return " Connection reset by peer";
#endif
#ifdef ENOBUFS
    if (errornum==ENOBUFS)
        return "No buffer space available";
#endif
#ifdef EISCONN
    if (errornum==EISCONN)
        return "Socket is already connected";
#endif
#ifdef ENOTCONN
    if (errornum==ENOTCONN)
        return "Socket is not connected";
#endif
#ifdef ESHUTDOWN
    if (errornum==ESHUTDOWN)
        return " Cannot send after socket shutdown";
#endif
#ifdef ETIMEDOUT
    if (errornum==ETIMEDOUT)
        return "Connection timed out";
#endif
#ifdef ECONNREFUSED
    if (errornum==ECONNREFUSED)
        return "Connection refused";
#endif
    return "Unknown error";
}
#endif

#ifndef HAVE_GETADDRINFO

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    /* Only support IPv4 if OS doesn't provide this function. */
    struct sockaddr_in sin;

    if (hints && hints->ai_family != AF_INET)
        return EAI_FAMILY;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;

    if (node) {
        if (hints && hints->ai_flags & AI_NUMERICHOST) {
#if HAVE_INET_ATON
            if (!inet_aton(node, &sin.sin_addr))
                return EAI_NONAME;
#else
            sin.sin_addr.s_addr = inet_addr(node);
            if (sin.sin_addr.s_addr == INADDR_NONE)
                return EAI_NONAME;
#endif
        } else {
            struct hostent *he;
            he = gethostbyname(node);
            if (!he)
                return EAI_NONAME;
            memcpy(&sin.sin_addr, he->h_addr, he->h_length);
        }
    } else if (hints && hints->ai_flags & AI_PASSIVE) {
        /* leave it unspecifed */
    } else {
        inet_aton("127.0.0.1", &sin.sin_addr);
    }

    if (!service)
        sin.sin_port = ntohs(0);
    else if (!(sin.sin_port = ntohs(atoi(service))))
        return EAI_NONAME;

    *res = calloc(1, sizeof(**res) + sizeof(sin));
    (*res)->ai_family = sin.sin_family;
    (*res)->ai_socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    (*res)->ai_protocol = hints && hints->ai_socktype ? hints->ai_socktype : 0;
    (*res)->ai_addrlen = sizeof(sin);
    (*res)->ai_addr = (struct sockaddr*)(*res + 1);
    memcpy((*res)->ai_addr, &sin, (*res)->ai_addrlen);
    (*res)->ai_canonname = 0;
    (*res)->ai_next = 0;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    struct addrinfo *next;
    for (; res; res = next) {
        next = res->ai_next;
        free(res);
    }
}
#endif

#ifndef HAVE_GAI_STRERROR
const char *gai_strerror(int errcode)
{
    switch (errcode) {
#if defined(EAI_ADDRFAMILY)
    case EAI_ADDRFAMILY: return "Address family not supported.";
#endif
#if defined(EAI_AGAIN)
    case EAI_AGAIN: return "A temporary failure occurred during name resolution.";
#endif
#if defined(EAI_BADFLAGS)
    case EAI_BADFLAGS: return "Invalid flags hint.";
#endif
#if defined(EAI_FAIL)
    case EAI_FAIL: return "An unrecoverable failure occurred during name resolution.";
#endif
#if defined(EAI_FAMILY)
    case EAI_FAMILY: return "Address family not supported.";
#endif
#if defined(EAI_MEMORY)
    case EAI_MEMORY: return "Not enough memory.";
#endif
#if defined(EAI_NODATA)
    case EAI_NODATA: return "The name resolves to an empty record.";
#endif
#if defined(EAI_NONAME)
    case EAI_NONAME: return "The name does not resolve.";
#endif
#if defined(EAI_OVERFLOW)
    case EAI_OVERFLOW: return "Resolved name was too large for buffer.";
#endif
#if defined(EAI_SERVICE)
    case EAI_SERVICE: return "The socket type does not support the requested service.";
#endif
#if defined(EAI_SOCKTYPE)
    case EAI_SOCKTYPE: return "Unknown socket type.";
#endif
#if defined(EAI_SYSTEM)
    case EAI_SYSTEM: return "A system error occurred during name resolution.";
#endif
    }
    return "Unknown GAI_* error";
}
#endif

#ifndef HAVE_GETNAMEINFO
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, size_t hostlen,
                char *serv, size_t servlen, int flags)
{
    if (sa->sa_family == AF_INET) {
	const struct sockaddr_in *sin;
	int tmp;

	/* This comparison is a bit screwy looking, but socklen_t is
	 * signed on some platforms and unsigned on others, so gcc
	 * will complain if we use "salen < 0".
	 */
	if (salen < 1 || (size_t)salen < sizeof(*sin))
	    return EAI_FAMILY;
	sin = (const struct sockaddr_in *)sa;
	tmp = snprintf(serv, servlen, "%d", ntohs(sin->sin_port));
	if (tmp < 1 || (size_t)tmp >= servlen)
	    return EAI_OVERFLOW;
	if (0 == (flags & NI_NUMERICHOST)) {
	    struct hostent *he;

	    /* Try to get host entry by address.
	     * The first argument should be void *, but Cygwin is
	     * apparently wandering around the pre-C89 era.
	     */
	    he = gethostbyaddr((const char*)&sin->sin_addr, sa->sa_family, SOCK_STREAM);
	    if (he != NULL) {
		if (servlen <= strlen(he->h_name))
		    return EAI_OVERFLOW;
		safestrncpy(serv, he->h_name, servlen);
		return 0;
	    }

	    /* If we couldn't, why did we fail, and what should we do? */
	    switch (h_errno) {
	    case NO_RECOVERY:
		return EAI_FAIL;
	    case TRY_AGAIN:
		return EAI_AGAIN;
	    default:
		/* Fall through and out to inet_ntop() path. */
		break;
	    }
	}

	/* Try to get numeric representation of address. */
	if (inet_ntop(sa->sa_family, &sin->sin_addr, host, hostlen) != NULL)
	    return 0;
	else if (errno == ENOSPC)
	    return EAI_OVERFLOW;
	else
	    return EAI_FAIL;
    }
    else
	return EAI_FAMILY;
}
#endif
