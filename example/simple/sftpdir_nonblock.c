/*
 * $Id: sftpdir_nonblock.c,v 1.2 2007/04/22 11:00:02 jehousley Exp $
 *
 * Sample doing an SFTP directory listing.
 *
 * The sample code has default values for host name, user name, password and
 * path, but you can specify them on the command line like:
 *
 * "sftpdir 192.168.0.1 user password /tmp/secretdir"
 */

#include <libssh2.h>
#include <libssh2_sftp.h>

#ifndef WIN32
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
# include <arpa/inet.h>
#else
# include <winsock2.h>
#endif

#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

int main(int argc, char *argv[])
{
	unsigned long hostaddr;
	int sock, i, auth_pw = 1;
	struct sockaddr_in sin;
	const char *fingerprint;
	LIBSSH2_SESSION *session;
	char *username=(char *)"username";
	char *password=(char *)"password";
	char *sftppath=(char *)"/tmp/secretdir";
	int rc;
	LIBSSH2_SFTP *sftp_session;
	LIBSSH2_SFTP_HANDLE *sftp_handle;

#ifdef WIN32
	WSADATA wsadata;

	WSAStartup(WINSOCK_VERSION, &wsadata);
#endif

	if (argc > 1) {
		hostaddr = inet_addr(argv[1]);
	} else {
		hostaddr = htonl(0x7F000001);
	}

	if(argc > 2) {
		username = argv[2];
	}
	if(argc > 3) {
		password = argv[3];
	}
	if(argc > 4) {
		sftppath = argv[4];
	}
	/*
	 * The application code is responsible for creating the socket
	 * and establishing the connection
	 */
	sock = socket(AF_INET, SOCK_STREAM, 0);

	sin.sin_family = AF_INET;
	sin.sin_port = htons(22);
	sin.sin_addr.s_addr = hostaddr;
	if (connect(sock, (struct sockaddr*)(&sin),
		    sizeof(struct sockaddr_in)) != 0) {
		fprintf(stderr, "failed to connect!\n");
		return -1;
	}

	/* We set the socket non-blocking. We do it after the connect just to
	   simplify the example code. */
#ifdef F_SETFL
	/* FIXME: this can/should be done in a more portable manner */
	rc = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, rc | O_NONBLOCK);
#else
#error "add support for setting the socket non-blocking here"
#endif

	/* Create a session instance
	 */
	session = libssh2_session_init();
	if(!session)
		return -1;

	/* ... start it up. This will trade welcome banners, exchange keys,
	 * and setup crypto, compression, and MAC layers
	 */
	rc = libssh2_session_startup(session, sock);
	if(rc) {
		fprintf(stderr, "Failure establishing SSH session: %d\n", rc);
		return -1;
	}

	/* At this point we havn't yet authenticated.  The first thing to do
	 * is check the hostkey's fingerprint against our known hosts Your app
	 * may have it hard coded, may go to a file, may present it to the
	 * user, that's your call
	 */
	fingerprint = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_MD5);
	printf("Fingerprint: ");
	for(i = 0; i < 16; i++) {
		printf("%02X ", (unsigned char)fingerprint[i]);
	}
	printf("\n");

	if (auth_pw) {
		/* We could authenticate via password */
		if (libssh2_userauth_password(session, username, password)) {
			printf("Authentication by password failed.\n");
			goto shutdown;
		}
	} else {
		/* Or by public key */
		if (libssh2_userauth_publickey_fromfile(session, username,
							"/home/username/.ssh/id_rsa.pub",
							"/home/username/.ssh/id_rsa",
							password)) {
			printf("\tAuthentication by public key failed\n");
			goto shutdown;
		}
	}

	fprintf(stderr, "libssh2_sftp_init()!\n");
	sftp_session = libssh2_sftp_init(session);

	if (!sftp_session) {
		fprintf(stderr, "Unable to init SFTP session\n");
		goto shutdown;
	}

	/* Since we have set non-blocking, tell libssh2 we are non-blocking */
	libssh2_sftp_set_blocking(sftp_session, 0);
	
	fprintf(stderr, "libssh2_sftp_opendir()!\n");
	/* Request a dir listing via SFTP */
	sftp_handle = libssh2_sftp_opendir(sftp_session, sftppath);
	
	if (!sftp_handle) {
		fprintf(stderr, "Unable to open dir with SFTP\n");
		goto shutdown;
	}
	
	fprintf(stderr, "libssh2_sftp_opendir() is done, now receive listing!\n");
	do {
		char mem[512];
		LIBSSH2_SFTP_ATTRIBUTES attrs;

		/* loop until we fail */
		while ((rc = libssh2_sftp_readdirnb(sftp_handle, mem, sizeof(mem), &attrs)) == LIBSSH2SFTP_EAGAIN) {
			;
		}
		if(rc > 0) {
			/* rc is the length of the file name in the mem
			   buffer */

			if(attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
				/* this should check what permissions it
				   is and print the output accordingly */
				printf("--fix----- ");
			} else {
				printf("---------- ");
			}

			if(attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID) {
				printf("%4ld %4ld ", attrs.uid, attrs.gid);
			} else {
				printf("   -    - ");
			}

			if(attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
				/* attrs.filesize is an uint64_t according to
				   the docs but there is no really good and
				   portable 64bit type for C before C99, and
				   correspondingly there was no good printf()
				   option for it... */

				printf("%8lld ", attrs.filesize);
			}

			printf("%s\n", mem);
		}
		else if (rc == LIBSSH2SFTP_EAGAIN) {
			/* blocking */
			fprintf(stderr, "Blocking\n");
		} else {
			break;
		}

	} while (1);

	libssh2_sftp_closedir(sftp_handle);
	libssh2_sftp_shutdown(sftp_session);

 shutdown:

	libssh2_session_disconnect(session, "Normal Shutdown, Thank you for playing");
	libssh2_session_free(session);

#ifdef WIN32
	Sleep(1000);
	closesocket(sock);
#else
	sleep(1);
	close(sock);
#endif
printf("all done\n");
	return 0;
}