#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

#include <sys/stat.h>
#include <fcntl.h>

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
static char Ewstat[] = "wstat not supported";

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

static char *
tellpath(File *f)
{
	struct Entry *node;

	for(node = top.next; node != &top; node = node->next)
		if(node->file == f)
			return node->path;
	return nil;
}

static void
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
		(*node)->name = strrchr((*node)->path, '/');
		if((*node)->name == nil)
			(*node)->name = (*node)->path;
		else
			(*node)->name++;
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

		fd = open(first->path, O_RDONLY);
		if(fd == -1)
		{
			fprint(2, "%s: cannot open file.\n", first->path);
			continue;
		}
		if(lseek(fd, (off_t) 0, SEEK_CUR) == -1)
		{
			close(fd);
			fprint(2, "%s: not a regular file.\n", first->path);
			continue;
		}
		if(fstat(fd, &st) == -1)
		{
			close(fd);
			fprint(2, "%s: cannot stat file.\n", first->path);
			continue;
		}
		close(fd);
		first->file = createfile(root,
					 first->name,
					 nil,
					 0664,
					 nil);
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
	long n;

	f = r->fid->file;
	if(f->dir.qid.type & QTDIR)
	{
		r->ofcall.count = 0;
		respond(r, nil);
		return;
	}
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
	if(lseek(fd, (off_t) offset, SEEK_SET) == -1)
	{
		close(fd);
		respond(r, Eseek);
		return;
	}
	n = read(fd, r->ofcall.data, count);
	close(fd);
	if(n == -1)
	{
		respond(r, Eopen);
		return;
	}
	r->ofcall.count = n;
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
	long n;

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
	if(lseek(fd, (off_t) offset, SEEK_SET) == -1)
	{
		close(fd);
		respond(r, Eseek);
		return;
	}
	n = write(fd, r->ifcall.data, count);
	close(fd);
	if(n == -1)
	{
		respond(r, Eopen);
		return;
	}
	r->ofcall.count = n;
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
	int mode;

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
	mode = r->ifcall.mode & 3;
	if(mode == O_WRONLY || mode == O_RDWR)
	{
		if((st.st_mode & 0222) == 0)
		{
			respond(r, Eperm);
			return;
		}
	}
	if(r->ifcall.mode & O_TRUNC)
	{
		int fd;

		if((fd = open(path, O_WRONLY|O_TRUNC)) == -1)
		{
			respond(r, Eopen);
			return;
		}
		close(fd);
		st.st_size = 0;
	}
	f->dir.length = (vlong) st.st_size;
	respond(r, nil);
}

static void
fswstat(Req *r)
{
	respond(r, Ewstat);
}

static void
fsstat(Req *r)
{
	File *f;
	char *path;
	Dir *d;

	f = r->fid->file;
	path = tellpath(f);
	if(path == nil)
	{
		respond(r, Eunknown);
		return;
	}
	d = dirstat(path);
	if(d == nil)
	{
		respond(r, Estat);
		return;
	}
	/* sstat already duplicated r->d strings; free them before replacing. */
	free(r->d.name);
	free(r->d.uid);
	free(r->d.gid);
	free(r->d.muid);
	free(r->d.ext);
	memcpy(&r->d, d, sizeof r->d);
	if(d->name != nil)
		r->d.name = estrdup9p(d->name);
	if(d->uid != nil)
		r->d.uid = estrdup9p(d->uid);
	if(d->gid != nil)
		r->d.gid = estrdup9p(d->gid);
	if(d->muid != nil)
		r->d.muid = estrdup9p(d->muid);
	if(d->ext != nil)
		r->d.ext = estrdup9p(d->ext);
	free(d);
	respond(r, nil);
}

static Srv fs = {
	.open=		fsopen,
	.read=		fsread,
	.write=		fswrite,
	.create=	fscreate,
	.remove=	fsremove,
	.stat=		fsstat,
	.wstat=		fswstat
};

static void
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
