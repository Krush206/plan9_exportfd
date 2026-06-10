#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include <sys/stat.h>

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

static struct Entry top = {
	"",
	"",
	nil,
	&top
};

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
	return nil;
}

void
buildpath(File *root, char *vp[], int i)
{
	struct Entry **node;
	struct Entry *first;
	struct Entry *second;

	node = &top.next;
	while(--i)
	{
		Dir *d;

		*node = emalloc9p(sizeof **node);
		(*node)->path = vp[i];
		d = dirstat((*node)->path);
		if(d == nil || d->qid.type & QTDIR)
		{
			fprint(2, "%s: invalid file.\n", (*node)->path);
			threadexits("invalid file");
		}
		(*node)->name = strrchr((*node)->path, '/') + 1;
		node = &(*node)->next;
	}
	*node = &top;
	for(first = top.next; first != &top; first = first->next)
		for(second = first->next;
		    second != first;
		    second = second->next)
			if(strcmp(first->name, second->name) == 0)
			{
				fprint(2, "%s: ambiguous.\n", first->name);
				threadexits("ambiguous");
			}
	for(first = top.next; first != &top; first = first->next)
	{
		struct stat st;
		int fd;

		first->file = createfile(root,
					 first->name,
					 nil,
					 0664,
					 nil);
		fd = open(first->path, O_RDONLY);
		if(lseek(fd, (off_t) 0, SEEK_CUR) == -1)
		{
			close(fd);
			continue;
		}
		if(fstat(fd, &st) == -1)
		{
			close(fd);
			continue;
		}
		close(fd);
		first->file->dir.length = (vlong) st.st_size;
	}
}

static void
fsread(Req *r)
{
	File *f;
	vlong offset;
	u32int count;
	char *path;
	int fd;

	f = r->fid->file;
	path = tellpath(f);
	if(path == nil)
	{
		respond(r, Eunknown);
		return;
	}
	offset = r->ifcall.offset;
	count = r->ifcall.count;
	if(count == 0)
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
	lseek(fd, (off_t) offset, SEEK_SET);
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
	int fd;

	f = r->fid->file;
	path = tellpath(f);
	if(path == nil)
	{
		respond(r, Eunknown);
		return;
	}
	offset = r->ifcall.offset;
	count = r->ifcall.count;
	if(count == 0)
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
	lseek(fd, (off_t) offset, SEEK_SET);
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
fsopen(Req *r)
{
	File *f;
	char *path;
	struct stat st;

	f = r->fid->file;
	if(f->dir.qid.type & QTDIR)
	{
		respond(r, nil);
		return;
	}
	path = tellpath(f);
	if(path == nil)
	{
		respond(r, Eunknown);
		return;
	}
	if(stat(path, &st) == -1)
	{
		respond(r, Estat);
		return;
	}
	f->dir.length = (vlong) st.st_size;
	respond(r, nil);
}

static void
fswstat(Req *r)
{
	respond(r, nil);
}

static Srv fs = {
	.open=		fsopen,
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
	threadexits("usage");
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
}
