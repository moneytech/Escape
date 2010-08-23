/**
 * $Id$
 * Copyright (C) 2008 - 2009 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <esc/common.h>
#include <esc/proc.h>
#include <esc/cmdargs.h>
#include <esc/dir.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

static bool run = true;

static void usage(const char *name) {
	fprintf(stderr,"Usage: %s [if=<file>] [of=<file>] [bs=N] [count=N]\n",name);
	fprintf(stderr,"	You can use the suffixes K, M and G to specify N\n");
	exit(EXIT_FAILURE);
}
static void interrupted(tSig sig,u32 data) {
	UNUSED(sig);
	UNUSED(data);
	run = false;
}

int main(int argc,const char *argv[]) {
	char apath[MAX_PATH_LEN];
	u32 bs = 4096;
	u32 count = 0;
	u64 total = 0;
	char *inFile = NULL;
	char *outFile = NULL;
	FILE *in = stdin;
	FILE *out = stdout;

	s32 res = ca_parse(argc,argv,CA_NO_DASHES | CA_NO_FREE | CA_REQ_EQ,
			"if=s of=s bs=k count=k",&inFile,&outFile,&bs,&count);
	if(res < 0) {
		fprintf(stderr,"Invalid arguments: %s\n",ca_error(res));
		usage(argv[0]);
	}
	if(ca_hasHelp())
		usage(argv[0]);

	if(setSigHandler(SIG_INTRPT,interrupted) < 0)
		error("Unable to set sig-handler for SIG_INTRPT");

	if(inFile) {
		abspath(apath,sizeof(apath),inFile);
		in = fopen(apath,"r");
		if(in == NULL)
			error("Unable to open '%s'",apath);
	}
	if(outFile) {
		abspath(apath,sizeof(apath),outFile);
		out = fopen(apath,"w");
		if(out == NULL)
			error("Unable to open '%s'",apath);
	}

	size_t result;
	u8 *buffer = (u8*)malloc(bs);
	u64 limit = (u64)count * bs;
	while(run && (!count || total < limit)) {
		if((result = fread(buffer,sizeof(u8),bs,in)) == 0)
			break;
		if(fwrite(buffer,sizeof(u8),bs,out) == 0)
			break;
		total += result;
	}
	if(ferror(in))
		error("Read failed");
	if(ferror(out))
		error("Write failed");
	free(buffer);

	printf("Wrote %Lu bytes in %.3f packages, each %u bytes long\n",
			total,(float)(total / (double)bs),bs);

	if(inFile)
		fclose(in);
	if(outFile)
		fclose(out);
	return EXIT_SUCCESS;
}
