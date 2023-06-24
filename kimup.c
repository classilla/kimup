/*
 * kimup KIM-1 serial loader v1.0
 * connect your KIM (at 300bps) and pass files or paper tape
 * (C)2023 cameron kaiser. all rights reserved. Floodgap Free Software License.
 *
 * cc -O3 -o kimup kimup.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

//#define DEBUG 1

// not reliable >300bps
#ifndef BAUDRATE
#define BAUDRATE B300
#endif

int port, spini = 0;
struct termios tty, tty_saved, tty_saved_in, tty_saved_out, tty_saved_err;
struct sigaction hothotsig;
unsigned char hex[16] = "0123456789ABCDEF";

void cleanup() {
	(void)tcsetattr(port, TCSAFLUSH, &tty_saved);
	(void)close(port);
	(void)tcsetattr(STDIN_FILENO, TCSANOW, &tty_saved_in);
	(void)tcsetattr(STDOUT_FILENO, TCSANOW, &tty_saved_out);
	(void)tcsetattr(STDERR_FILENO, TCSANOW, &tty_saved_err);
}

void
hotsigaction(int hotsig)
{
	fprintf(stdout, "\n\nexiting on signal\n");
	cleanup();
	exit(0);
}

void spin() {
	++spini; spini &= 3;
	fprintf(stderr, "\b%s",
		(spini == 0) ? "/" :
		(spini == 1) ? "-" :
		(spini == 2) ? "\\" :
		(spini == 3) ? "|" : " ");
}

int waitc(int fd, unsigned char c) {
	struct timeval tv;
	fd_set master, rfd;
	unsigned char cc;
	int rv;

	FD_ZERO(&master);
	FD_SET(fd, &master);

	for(;;) {
		rfd = master;
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		if ((rv = select(fd+1, &rfd, NULL, NULL, &tv)) < 0) {
			/* something's wrong */
			perror("waitc: select");
			return -1;
		}
		if (!rv || !FD_ISSET(fd, &rfd)) return 0; /* timed out */
		if (read(fd, &cc, 1)) {
#if DEBUG
fprintf(stderr, " %02x ", cc);
#endif
			if (c==255) return 1; /* don't care */
			if (c==cc) return 1;
			/* doesn't match, return to loop */
		} else {
			/* something's wrong */
			return -1;
		}
	}

	return 0; /* not reached */
}
int waitcx(int fd, unsigned char c) {
	int rv;

	rv = waitc(fd, c);
	if (rv < 0) {
		fprintf(stderr, "read connection to serial port failed\n");
		exit(255);
	}
	return rv;
}

int sendc(int fd, unsigned char c) {
	struct timeval tv;
	fd_set master;
	int rv;

	FD_ZERO(&master);
	FD_SET(fd, &master);

	/* immediately check if writeable */
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	if ((rv = select(fd+1, NULL, &master, NULL, &tv)) < 0) {
		/* something's wrong */
		perror("sendc: select");
		return 0;
	}
	if (!rv || !FD_ISSET(fd, &master)) return 0; /* can't write */
	return (write(fd, &c, 1)) ? 1 : 0;
}

#if DEBUG
#define WAIT(x) if(!waitcx(fd, x)) continue; fprintf(stderr, " !! ")
#else
#define WAIT(x) if(!waitcx(fd, x)) continue
#endif
int kimmon(int fd, int mode) {
	int rv, timeout = 0;

	tcflush(fd, TCIOFLUSH);
	fprintf(stderr, (mode) ? "waiting for completion  " :
		"getting kim's attention (press RS?)  ");
	for(;;) {
		spin();
		if(!mode && !sendc(fd, 0x7f)) {
			fprintf(stderr, "write connection to serial port failed\n");
			exit(255);
		}
		if (mode && ++timeout > 10) {
			fprintf(stderr, "\b \nerror: transmit FAILED\n");
			return 0;
		}
		WAIT('K');
		spin();
		WAIT('I');
		spin();
		WAIT('M');
		spin();
		WAIT(0x0a);
		spin();

		WAIT(0x20);
		spin();
		WAIT(0x20);
		spin();
		/* this should time out if KIM is ready */
		if (!waitcx(fd, 0xff)) break;
	}
	fprintf(stderr, "\b \n");
	tcflush(fd, TCIOFLUSH);
	return 1;
}

void whexb(unsigned char *ptr, unsigned char v) {
	ptr[0] = hex[v >> 4];
	ptr[1] = hex[v & 15];
}

#define USAGE "usage: %s [-g run_address] { -p papertape | address file [address file]... }\n"

int main(int argc, char **argv)
{
	int port, filein, adr, xadr, i, j, lines;
	ssize_t count;
	struct timeval tv;
	unsigned char c, bin[24], bout[256];
	unsigned short sum;

	/* it's not enough to just restore the serial port settings */
	if (tcgetattr(STDIN_FILENO, &tty_saved_in) ||
			tcgetattr(STDOUT_FILENO, &tty_saved_out) ||
			tcgetattr(STDERR_FILENO, &tty_saved_err)) {
		perror("tcgetattr (tty)");
		return 1;
	}

	if (argc < 3) {
		fprintf(stderr, USAGE, argv[0]);
		return 255;
	}
	xadr = -1;
	j = 1;
	if (argv[1][0] == '-' && argv[1][1] == 'g') {
		if (argc < 5) {
			fprintf(stderr, USAGE, argv[0]);
			return 255;
		}
		xadr = strtol(argv[2], NULL, 0);
		if (errno == EINVAL || xadr < 0 || xadr > 65535) {
			fprintf(stderr, "error: illegal run address\n");
			return 1;
		}
		j+=2;
	}

	port = -1;
	if (getenv("SERIALPORT")) {
#if DEBUG
		fprintf(stderr, "trying %s\n", getenv("SERIALPORT"));
#endif
		port = open(getenv("SERIALPORT"), O_RDWR);
	}
	if (port == -1) {
#if DEBUG
		fprintf(stderr, "trying /dev/ttyUSB0\n");
#endif
		port = open("/dev/ttyUSB0", O_RDWR);
	}
	if (port == -1) {
#if DEBUG
		fprintf(stderr, "trying /dev/cu.usbserial\n");
#endif
		port = open("/dev/cu.usbserial", O_RDWR);
	}
	if (tcgetattr(port, &tty)) {
		perror("tcgetattr");
		close(port);
		return 1;
	}
	tty_saved = tty;

	memset(&tty, 0, sizeof(tty));
	tty.c_cflag = CS8 | CREAD | CLOCAL; /* kills everything else */
	tty.c_cc[VMIN] = 1; /* but we select() anyway */
	tty.c_cc[VTIME] = 5;
	/* this effectively is the same as cfmakeraw() */
	cfsetospeed(&tty, BAUDRATE);
	cfsetispeed(&tty, BAUDRATE);
	tcsetattr(port, TCSANOW, &tty);

	atexit(cleanup);
	(void)memset(&hothotsig, 0, sizeof(hothotsig));
	hothotsig.sa_handler = hotsigaction;
	sigaction(SIGINT, &hothotsig, NULL);
	sigaction(SIGTERM, &hothotsig, NULL);
	sigaction(SIGHUP, &hothotsig, NULL);

#define KIM_WARM_START 0
#define KIM_POST_LOAD 1
	if (!kimmon(port, KIM_WARM_START) || !sendc(port, 'L')) {
		return 1;
	}
	tcdrain(port); // paranoia

	lines = 0;
	for(;;) {
		if (j >= argc) break;

		if (argv[j][0] == '-' && argv[j][1] == 'p') {
			if (lines) {
				fprintf(stderr, "error: raw paper tape must be the only file\n");
				return 1;
			}
			adr = -1;
			j++;
		} else {
			adr = strtol(argv[j++], NULL, 0);
			if (errno == EINVAL || adr < 0 || adr > 65535) {
				fprintf(stderr, "error: illegal address\n");
				return 1;
			}
		}
		filein = open(argv[j], O_RDONLY);
		if (filein < 0) {
			perror("open");
			return 1;
		}

		if (adr == -1) {
			// paper tape
			if (++j < argc) {
				fprintf(stderr, "warning: extra arguments ignored\n");
			}
			fprintf(stderr, "transmitting raw paper tape  ");
			spin();
			for(;;) {
				// go byte by byte, no reason to do otherwise
				if (!read(filein, &c, 1)) break;
				// the KIM echoes every byte back
				if (!sendc(port, c) || !waitcx(port, 255)) {
					return 1;
				}
				if (c == '\n') {
					spin();
					tcdrain(port);
				}
			}
			tcdrain(port);
			close(filein);
			lines = 0; // already sent
			fprintf(stderr, "\b \n");
			break;
		}

		fprintf(stderr, "uploading %s to address $%04x  ", argv[j++], adr);
		for(;;) {
			sum = 0;
			bout[0] = ';';

			spin();
			count = read(filein, &bin, 24);
			if (!count) break;

			whexb(&bout[1], count);
			whexb(&bout[3], (adr >> 8));
			whexb(&bout[5], (adr & 255));
			for(i=0; i<count; i++) {
				whexb(&bout[7+i+i], bin[i]);
				sum += bin[i];
			}
			sum += count;
			sum += (adr >> 8);
			sum += (adr & 255);
			whexb(&bout[7+count+count], (sum >> 8));
			whexb(&bout[9+count+count], (sum & 255));
			bout[11+count+count] = '\n';
#if DEBUG
write(fileno(stdout), bout, 12+count+count);
#endif
			write(port, bout, 12+count+count);
			tcdrain(port);
			// read back everything
			read(port, bout, 12+count+count);

			lines++;
			if (count != 24) break;
			adr += count;
		}
		fprintf(stderr, "\b \n");
		close(filein);
	}
	if (lines) {
		bout[0] = ';';
		bout[1] = '0';
		bout[2] = '0';
		whexb(&bout[3], (lines >> 8));
		whexb(&bout[7], (lines >> 8));
		whexb(&bout[5], (lines & 255));
		whexb(&bout[9], (lines & 255));
		bout[11] = '\n';
#if DEBUG
write(fileno(stdout), bout, 12);
#endif
		write(port, bout, 12);
		tcdrain(port);
		read(port, bout, 12);
	}
	if (!kimmon(port, KIM_POST_LOAD)) {
		return 1;
	}
	if (xadr >= 0) {
		fprintf(stderr, "executing from $%04x\n", xadr);
		tcflush(port, TCIOFLUSH);
		whexb(&bout[0], (xadr >> 8));
		whexb(&bout[2], (xadr & 255));
		bout[4] = ' ';
#if DEBUG
write(fileno(stdout), bout, 5);
#endif
		write(port, bout, 5);
		tcdrain(port);
		waitcx(port, ' ');
#if DEBUG
fprintf(stderr, " !! ");
#endif
		waitcx(port, ' ');
#if DEBUG
fprintf(stderr, " !! ");
#endif
		waitcx(port, ' ');
		sendc(port, 'G');
	} else {
		fprintf(stderr, "complete!\n");
	}

	tcdrain(port);
	close(port);
	return 0;
}
