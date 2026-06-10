#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <stdio.h>

#include <sys/stat.h>

#undef exits

struct Entry {
	char *path;
	char *name;
	File *file;
	struct Entry *next;
};

static char Estat[] = "cannot stat file";
static char Eopen[] = "cannot open file";
static char Eseek[] = "cannot seek file";
static char Eunknown[] = "unknown file";
static char Eperm[] = "permission denied";

static struct Entry top;

int
threadmaybackground(void)
{
	return 1;
}

char *
tellpath(File *f)
{
	struct Entry *node;

	for(node = top.next; node != &top; node = node->next)
		if(node->file == f)
			return node->path;
	return NULL;
}

void
buildpath(File *root, char *vp[], int i)
{
	struct Entry **node;

	node = &top.next;
	while(--i)
	{
		Dir *d;

		*node = malloc(sizeof **node);
		(*node)->path = vp[i];
		d = dirstat((*node)->path);
		if(d->mode & DMDIR)
		{
			fprint(2, "%s: invalid file.\n", (*node)->path);
			exits("invalid file");
		}
		(*node)->name = strrchr((*node)->path, '/') + 1;
		(*node)->file = createfile(root,
					   (*node)->name,
					   nil,
					   0664,
					   nil);
		node = &(*node)->next;
	}
	*node = &top;
}

static void
fsread(Req *r)
{
	File *f;
	vlong offset;
	u32int count;
	char *path;
	struct stat st;
	int fd;

	f = r->fid->file;
	path = tellpath(f);
	if(path == NULL)
	{
		respond(r, Eunknown);
		return;
	}
	if(stat(path, &st) == -1)
	{
		respond(r, Estat);
		return;
	}
	offset = r->ifcall.offset;
	count = r->ifcall.count;
	if(offset + count > st.st_size || count == 0)
	{
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	if((fd = open(path, O_RDONLY)) == -1)
	{
		respond(r, Eopen);
		return;
	}
	if(lseek(fd, offset, SEEK_SET) == -1)
	{
		close(fd);
		respond(r, Eseek);
		return;
	}
	r->ofcall.count = read(fd, r->ofcall.data, count);
	close(fd);
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	File *f;
	vlong offset;
	u32int count;
	char *path;
	struct stat st;
	int fd;

	f = r->fid->file;
	path = tellpath(f);
	if(path == NULL)
	{
		respond(r, Eunknown);
		return;
	}
	if(stat(path, &st) == -1)
	{
		respond(r, Estat);
		return;
	}
	offset = r->ifcall.offset;
	count = r->ifcall.count;
	if(offset + count > st.st_size || count == 0)
	{
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
	if((fd = open(path, O_WRONLY)) == -1)
	{
		respond(r, Eopen);
		return;
	}
	if(lseek(fd, offset, SEEK_SET) == -1)
	{
		close(fd);
		respond(r, Eseek);
		return;
	}
	r->ofcall.count = write(fd, r->ifcall.data, count);
	close(fd);
	respond(r, nil);
}

static void
fscreate(Req *r)
{
	respond(r, Eperm);
}

static void
fsremove(Req *r)
{
	respond(r, Eperm);
}

static void
fswstat(Req *r)
{
	respond(r, nil);
}

static Srv fs = {
	.read=		fsread,
	.write=		fswrite,
	.create=	fscreate,
	.remove=	fsremove,
	.wstat=		fswstat
};

void
usage(void)
{
	fprint(2, "usage: %s file ...\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	char *srvname;

	argv0 = argv[0];
	if(argc == 1)
		usage();
	fs.tree = alloctree(nil, nil, 0777 | DMDIR, nil);
	buildpath(fs.tree->root, argv, argc);
	srvname = "exportfd";
	threadpostmountsrv(&fs, srvname, nil, MBEFORE);
	threadmaybackground();
}
