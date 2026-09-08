#include "libgs.h"
#include "launchelf.h"
#include <audsrv.h>
#include "audsrv2.h"
#define	MAX_LINES	32768
#define	MAX_COLS	16384
#define	MAX_UNICODE	0x010000
//#define	waitPadReady(...)
//#define	BBSVIEWER

typedef struct {
	unsigned int offset;
	unsigned int bytes;
} ofscache;

static unsigned int ft_type[FT_TYPES] = {
	FT_ELF, FT_EXE,
	FT_JPG, FT_PNG, FT_GIF, FT_BMP, FT_P2T, FT_PS1, FT_ICO,
	FT_MP3, FT_AAC, FT_AC3, FT_PCM, FT_MID,
	FT_TXT, FT_XML, FT_HTM,
	FT_ZIP, FT_RAR, FT_LZH, FT_TEK, FT_GZ, FT_7Z,
	FT_AVI, FT_MPG, FT_MP4,
	FT_FNT,
	0
};
static unsigned char ft_char[FT_TYPES][4] = {
	"ELF", "EXE",
	"JPG", "PNG", "GIF", "BMP", "ICO", "PS1", "ICO",
	"MP3", "AAC", "AC3", "WAV", "MID",
	"TXT", "XML", "HTM",
	"ZIP", "RAR", "LZH", "TEK", "GZ ", "7Z ",
	"AVI", "MPG", "MP4",
	"FNT",
};
enum{
	TTP_TEXT,
	TTP_BINARY,
	TTP_BBS,
};
enum{
	BBS_TEMPSIZE = 65536,
};
//static unsigned char *clipbuffer=NULL;
//static unsigned char editline[2][MAX_COLS];
static unsigned char displine[MAX_COLS];
static int redraw=2, charset=0, fullscreen=0, resizer=1;
int linenum=0, defaultcharset=0, tabmode=8, tabdisp=0, nldisp=0, aniauto=1, imgpos=4;
extern unsigned short sjistable[];
//extern unsigned short ucstable[];
static unsigned short ucstable[MAX_UNICODE];
static int ucstabled=0;
static int wordwrap=0;
uint64 *activeclut=NULL;
static int alphablend=0;
//static ofscache *line[];
static int chartable[] = {TXT_AUTO, TXT_ASCII, TXT_SJIS, TXT_EUCJP, TXT_JIS, TXT_UTF8, -1};
static char chartablename[][8] = {"AUTO", "ASCII", "SJIS", "EUCJP", "JIS", "UTF8", ""};
static unsigned char ctrlchars[32];
extern unsigned char sndview_filename[MAX_PATH], sndview_format[32], sndview_totaltime[16], *sndview_head, *sndview_body;
extern int nobgmpos;
extern unsigned int sndview_size;
int scrnsize=0; char *scrnbuff=NULL;
//*/
extern unsigned char monafontwidth[11536];
//int txt_convert_encoding(unsigned char *dist, unsigned char *src, int dist_char, int src_char);
int txtdraw(unsigned char *buffer, unsigned int size, int charcode);
int utftosjis2(unsigned char *dist, unsigned char *src, unsigned int limit, unsigned int size);
int euctosjis2(unsigned char *dist, unsigned char *src, unsigned int limit, unsigned int size);
int jistosjis2(unsigned char *dist, unsigned char *src, unsigned int limit, unsigned int size);
int ucstableinit();

int info_BMP(int *info, char *src, int size);
int decode_BMP(char *dist, char *src, int size, int bpp);
int info_JPEG(void *env, int *info, int size, unsigned char *fp);
int decode0_JPEG(void *env, int size, unsigned char *fp, int b_type, unsigned char *buf, int skip);
int decode0_JPEGpart(void *env, int xsz, int ysz, int x0, int y0, int size, unsigned char *fp, int b_type, unsigned char *buf, int skip);
int info_GIF(int *info, char *src, int size);
int decode_GIF(char *dist, char *src, int size, int bpp);
int info_PS2ICO(int *info, char *src, int size);
int decode_PS2ICO(char *dist, char *src, int size, int bpp);
int info_PS1ICO(int *info, char *src, int size);
int decode_PS1ICO(char *dist, char *src, int size, int bpp);
int decode_PNG(char *dist, char *src, int size, int bpp);
int info_PNG(int *info, char *buff, int size);

////////////////////////////////
// デバッグ用トラップ
int viewmallocs=0;
static void *X_malloc(size_t mallocsize)
{
	void *ret;
	ret = malloc(mallocsize);
	if (ret == NULL)
		printf("viewer: malloc failed (ofs: %08X, size: %d)\n", (unsigned int) ret, mallocsize);
	else
		printf("viewer: malloc valid (ofs: %08X, size: %d) [%d]\n", (unsigned int) ret, mallocsize, ++viewmallocs);
	return ret;
}
static void X_free(void *mallocdata)
{
	if (mallocdata != NULL) {
		printf("viewer: free valid (ofs: %08X) [%d]\n", (unsigned int) mallocdata, --viewmallocs);
		free(mallocdata);
	} else 
		printf("viewer: free failed (ofs: %08X)\n", (unsigned int) mallocdata);
	mallocdata = NULL;
}
static void *X_realloc(void *mallocdata, size_t mallocsize)
{
	void *ret;
	ret = realloc(mallocdata, mallocsize);
	if (ret != NULL)
		printf("viewer: realloc valid (ofs: %08X -> %08X, size: %d)\n", (unsigned int) mallocdata, (unsigned int) ret, mallocsize);
	else
		printf("viewer: realloc failed: (ofs: %08X, size: %d)\n", (unsigned int) mallocdata, mallocsize);
	return ret;
}
#define	malloc	X_malloc
#define free	X_free
#define realloc	X_realloc
//*/

void makectrlchars(void)
{
	int i;
	for (i=0; i<32; i++)
		ctrlchars[i] = i+64;
	if (nldisp) {
		ctrlchars[13] = '<';
		ctrlchars[10] = 'v';
	} else {
		ctrlchars[13] = ' ';
		ctrlchars[10] = ' ';
	}
	if (tabdisp) ctrlchars[9] = '>'; else ctrlchars[9] = ' ';
}
////////////////////////////////
// 文字コードの判定
//	in:	*buffer	テキストデータ
//		size	テキストサイズ
//	out:(ret)	文字コード
int txt_detect(unsigned char *c, unsigned int size, int *type)
{
	int w,x,y,z,u,charset,cp,co;
	int ascii=0,jis=0,sjis=0,eucjp=0,utf8=0,text=0,binary=0,bbs=0;
	int jisb=0,jisf=0,jism=0,sjisf=0,eucjpf=0,utf8f=0,bbsf=0;
	// 文字コードを判定
	printf("textdetect: detecting charset...\n");
	for (x=0;x<size;x++) {
		y = x + 1; z = y + 1; w = z + 1;
		if (y>=size) y=size-1;
		if (z>=size) z=size-1;
		if (w>=size) w=size-1;
		if ((c[x] > 0) && (c[x] <= 0x7F)) ascii++;
		if ((c[x] == 0xCD) && (c[y] == 0xCD)) ascii+=4;
		if ((c[x] == 0xC4) && (c[y] == 0xC4)) ascii+=4;
		if ((c[x] == 0x80) || (c[x] == 0xA0) || (c[x] > 0xFC)) ascii+=2;
		if ((c[x] == 0x3C) && (c[y] == 0x3E)) bbsf++;
		else if (c[x] == 10) {
			if (bbsf != 4) bbs -= 5; else bbs += 30;
			bbsf = 0;
		}
		if (sjisf == 0) {
			if (((c[x] > 0x00) && (c[x] <= 0x7F)) || ((c[x] >= 0xA1) && (c[x] <= 0xDF))) {
				sjisf=1;
			} else if ((((c[x] >= 0x81) && (c[x] <= 0x9F)) || ((c[x] >= 0xE0) && (c[x] <= 0xFC))) && (c[y] >= 0x40) && (c[y] != 0x7F) && (c[y] <= 0xFC)) {
				sjisf=2;
			}
			sjis+=sjisf;
			if (sjisf == 0) sjis--;
		}
		if (sjisf > 0) sjisf--;
		if (eucjpf == 0) {
			if ((c[x] > 0x00) && (c[x] <= 0x7F)) {
				eucjpf=1;
			} else if ((c[x] >= 0xA1) && (c[x] <= 0xFE) && (c[y] >= 0xA1) && (c[y] <= 0xFE)) {
				eucjpf=2;
			} else if ((c[x] == 0x8E) && (c[y] >= 0xA1) && (c[y] <= 0xDF)) {
				eucjpf=2;
			} else if ((c[x] == 0x8F) && (c[y] >= 0xA1) && (c[y] <= 0xFE) && (c[z] >= 0xA1) && (c[z] <= 0xFE)) {
				eucjpf=3;
			}
			eucjp+=eucjpf;
			if (eucjpf == 0) eucjp--;
		}
		if (eucjpf > 0) eucjpf--;
		if (jisf == 0) {
			if ((c[x] == 0x1B) && ((c[y] == 0x24) || (c[y] == 0x26)) && ((c[z] == 0x40) || (c[z] == 0x42))) {
				jisf=3; jism=3;
			} else if ((c[x] == 0x1B) && (c[y] == 0x28) && (c[z] == 0x42)) {
				jisf=3; jism=0;
			} else if ((c[x] == 0x1B) && (c[y] == 0x28) && (c[z] == 0x4A)) {
				jisf=3; jism=1;
			} else if ((c[x] == 0x1B) && (c[y] == 0x28) && (c[z] == 0x49)) {
				jisf=3; jism=2;
			} else if (c[x] == 0x0E) {
				jisb=jism; jism=2; jisf=1;
			} else if (c[x] == 0x0F) {
				jism=jisb; jisb=0; jisf=1;
			} else if (jism == 1) {
				if ((c[x] < 0x80) || ((c[x] >= 0xA1) && (c[x] <= 0xDF)))
					jisf=1;
			} else if (jism == 2) {
				if ((c[x] >= 0x21) && (c[x] <= 0x5F))
					jisf=1;
			} else if (jism == 3) {
				if ((c[x] < 0x21) || (c[x] > 0x7E) || (c[y] < 0x21) || (c[y] > 0x7E))
					jis-=4;
				jisf=2;
			} else if ((c[x] > 0) && (c[x] != 27) && (c[x] < 0x80)) {
				jisf=1;
			} else {
				jis--;
			}
			jis+=jisf+(jism>1);
			if (jisf == 0) jis--;
		}
		if (jisf > 0) jisf--;
		if (utf8f == 0) {
			if ((c[x] > 0x00) && (c[x] <= 0x7F)) {
				utf8f=1;
			} else if ((c[x] >= 0xC0) && (c[x] <= 0xE0) && (c[y] >= 0x80) && (c[y] <= 0xBF)) {
				u = 0x40 * (c[x] & 0x1F) + (c[y] & 0x3F);
				if (u >= 0x80) {
					utf8f=2;
				}
			} else if ((c[x] >= 0xE0) && (c[x] <= 0xF0) && (c[y] >= 0x80) && (c[y] <= 0xBF) && (c[z] >= 0x80) && (c[z] <= 0xBF)) {
				u = 0x1000 * (c[x] & 0x0F) + 0x40 * (c[y] & 0x3F) + (c[z] & 0x3F);
				if (((u >= 0x800) && (u < 0xD800)) || (u >= 0xE000)) {
					utf8f=3;
				}
			} else if ((c[x] >= 0xF0) && (c[x] <= 0xF8) && (c[y] >= 0x80) && (c[y] <= 0xBF) && (c[z] >= 0x80) && (c[z] <= 0xBF) && (c[w] >= 0x80) && (c[w] <= 0xBF)) {
				u = 0x40000 * (c[x] & 0x07) + 0x1000 * (c[y] & 0x3F) + 0x40 * (c[z] & 0x3F) + (c[w] & 0x3F);
				if ((u >= 0x10000) && (u <= 0x10FFFF)) {
					utf8f=4;
				}
			}
			utf8+=utf8f;
			if (utf8f == 0) utf8--;
		}
		if (utf8f > 0) utf8f--;
		if (((c[x] != 13) && (c[x] != 10) && (c[x] != 9) && (c[x] < 0x20)) || (c[x] > 0xFE)) binary++;
		if ((c[x] == 0) || (c[x] == 0xFF)) binary+=4;
		if ((c[x] >= 0x40) && (c[x] < 0x7F) && (c[y] >= 0x40) && (c[y] < 0x7F)) text++;
	}
	charset = TXT_SJIS; cp = sjis;
	if (eucjp >= cp)	{charset = TXT_EUCJP; cp = eucjp;}
	if (utf8 >= cp) 	{charset = TXT_UTF8; cp = utf8;	}
	if (jis >= cp)		{charset = TXT_JIS; cp = jis;}
	if (ascii >= cp)	{charset = TXT_ASCII; cp = ascii;}
	if (binary >= cp)	{charset = TXT_BINARY; cp = binary;}
	co = TTP_TEXT; cp = text;
	if (binary > cp)	{co = TTP_BINARY; cp = binary;}
	if (bbs > cp)		{co = TTP_BBS; cp = bbs;}
	if (type != NULL)	{*type = co;}
	printf("textdetect:%d txt=%d,bin=%d,2ch=%d,\n	asc=%d,sjis=%d,euc=%d,jis=%d,utf8=%d pts.\n", charset, text, binary, bbs, ascii, sjis, eucjp, jis, utf8);
	return charset;
}

int txt_count(unsigned char *c, unsigned int size, ofscache *dist, int maxcols, int maxrows)
{
	unsigned int x, y;
	int t=0, line=0, maxcol;
	
	maxcol = maxcols;
	if (maxcol < 0) maxcol = MAX_ROWS_X;
	if (maxcol == 0) maxcol = size+1;
	dist[line].offset = 0;
	dist[line].bytes = 0;
	
	printf("textcount: counting lines...\n");
	for (x=0; x<size; x++) {
		y = x + 1;
		if (y >= size) y = x;
		if ((c[x] == 13) || (c[x] == 10)) {
			if ((c[x] != c[y]) && ((c[y] == 13) || (c[y] == 10))) {
				dist[line].bytes++;
				x++;
			}
			t = 1;
		}
		if ((++dist[line].bytes >= maxcol) || t) {
			t = 0;
			if (++line < maxrows) {
				dist[line].offset = x+1;
				dist[line].bytes = 0;
			} else {
				printf("textcount: abort the line count.\n");
				break;
			}
		}
	}
	if (++line > maxrows) line = maxrows;
	printf("textcount: result is %d line(s).\n", line);
	return line;
}
int viewer_file(int mode, char *file)
{
	int fd,ret;
	unsigned char *buffer;
	unsigned int size;
	
	if (strcmp(file, sndview_filename)==0) {
		buffer = sndview_head;
		size = sndview_size;
	} else {
		printf("viewer: mode: %d\nviewer: file: %s\n", mode, file);
		fd = nopen(file, O_RDONLY);
		if (fd < 0)
			return -1;
		size = nseek(fd, 0, SEEK_END);
		printf("viewer: size: %d\n", size);
		nseek(fd, 0, SEEK_SET);
		buffer = (char*)malloc(size);
		if (buffer != NULL) {
			drawMsg(lang->gen_loading);
			nread(fd, buffer, size);
		}
		nclose(fd);
	}
	if (buffer == NULL)
		return -2;
	ret = viewer(mode, file, buffer, size);
	if (sndview_head != buffer)
		free(buffer);
	return ret;
}

int formatcheck(unsigned char *c, unsigned int size)
{
	int type=FT_TXT;
	if ((size >= 32) && (c[0] == 0xFF) && (c[1] == 0xD8) && (c[2] == 0xFF)) {
		type = FT_JPG;
	} else if ((size >= 16) && (c[0] == 0x89) && (c[1] == 0x50) && (c[2] == 0x4E) && (c[3] == 0x47) && (c[4] == 0x0D) && (c[5] == 0x0A)) {
		type = FT_PNG;
	} else if ((size >= 8) && (c[0] == 0x47) && (c[1] == 0x49) && (c[2] == 0x46) && (c[3] == 0x38) && ((c[4] == 0x37) || (c[4] == 0x39))) {
		type = FT_GIF;
	} else if ((size >= 14) && (c[0] == 0x42) && (c[1] == 0x4D) && (c[6] == 0x00) && (c[7] == 0x00) && (c[8] == 0x00) && (c[9] == 0x00)) {
		type = FT_BMP;
	} else if ((size >= 20) && (c[0] == 0) && (c[1] == 0) && (c[2] == 1) && (c[3] == 0) && (c[4] < 16) && (c[5] == 0) && (c[6] == 0) && (c[7] == 0) && (c[9] == 0) && (c[10] == 0) && (c[11] == 0)) {
		type = FT_P2T;
	} else if ((size >= 256) && (c[0] == 0x53) && (c[1] == 0x43) && (c[2] > 0x10) && (c[2] < 0x20) && (c[3] > 0) && (c[3] < 16)) {
		type = FT_PS1;
	} else if ((size >= 64) && (c[0] == 0x7F) && (c[1] == 0x45) && (c[2] == 0x4C) && (c[3] == 0x46) && (c[4] == 0x01) && (c[5] == 0x01)) {
		type = FT_ELF;
	} else if ((size >= 6) && (c[0] == 0x52) && (c[1] == 0x61) && (c[2] == 0x72) && (c[3] == 0x21) && (c[4] == 0x1A) && (c[5] == 0x07)) {
		type = FT_RAR;
	} else if ((size >= 4) && (c[0] == 0x50) && (c[1] == 0x4B) && (c[2] == 0x03) && (c[3] == 0x04)) {
		type = FT_ZIP;
	} else if ((size >= 8) && (c[2] == 0x2D) && (c[3] == 0x6C) && (c[4] == 0x68) && (c[6] == 0x2D)) {
		type = FT_LZH;
	} else if ((size >= 4) && (c[0] == 0x1F) && (c[1] == 0x8B)) {
		type = FT_GZ;
	} else if ((size >= 24) && (c[1] == 0xFF) && (c[2] == 0xFF) && (c[3] == 0xFF) && (c[4] == 0x01) && (c[5] == 0x00) && (c[6] == 0x00) && (c[7] == 0x00) && (c[8] == 0x4F) && (c[9] == 0x53) && (c[10] == 0x41) && (c[11] == 0x53) && (c[12] == 0x4B) && (c[13] == 0x43) && (c[14] == 0x4D) && (c[15] == 0x50)) {
		type = FT_TEK;
	} else if ((size >= 24) && (c[0] == 0x4D) && (c[1] == 0x54) && (c[2] == 0x68) && (c[3] == 0x64)) {
		type = FT_MID;
	} else if ((size >= 44) && (c[0] == 0x52) && (c[1] == 0x49) && (c[2] == 0x46) && (c[3] == 0x46) && (c[8] == 0x57) && (c[9] == 0x41) && (c[10] == 0x56) && (c[11] == 0x45)) {
		type = FT_PCM;
	} else if ((size >= 32) && (c[28] == 0x3C) && !c[29] && !c[30] && !c[31] && (c[12] | c[13] | c[14] | c[15]) && (c[24] | c[25])) {
		type = FT_PCM;
	} else if ((size >= 64) && ( ((c[0] == 0xFF) && (c[1] >= 0xE0) && (c[1] & 0x06) && ((c[2] & 0xF0)!=0xF0) && ((c[2] & 0x0C)!=0x0C)) || ((c[0] == 0x49) && (c[1] == 0x44) && (c[2] == 0x33)) || ((c[0] == 0x46) && (c[1] == 0x4C) && (c[2] == 0x56) && (c[3] == 0x01)) )) {
		type = FT_MP3;
	} else if ((size >= 64) && (c[4] == 0x66) && (c[5] == 0x74) && (c[6] == 0x79) && (c[7] == 0x70) && (c[8] == 0x4D) && (c[9] == 0x34) && (c[10] == 0x41) && (c[11] == 0x20)) {
		type = FT_AAC;
	} else if ((size >= 64) && (c[4] == 0x66) && (c[5] == 0x74) && (c[6] == 0x79) && (c[7] == 0x70) && (c[8] == 0x6D) && (c[9] == 0x70) && (c[10] == 0x34) && (c[11] == 0x32)) {
		type = FT_AAC;
	} else if ((size >= 64) && (c[0] == 0x0B) && (c[1] == 0x77) && (c[4] == 0x14) && (c[5] == 0x20) && (c[6] == 0x43) && (c[7] == 0xFE)) {
		type = FT_AC3;
	} else if ((size >= 64) && (c[0] == 0x52) && (c[1] == 0x49) && (c[2] == 0x46) && (c[3] == 0x46) && (c[8] == 0x41) && (c[9] == 0x56) && (c[10] == 0x49) && (c[11] == 0x20)) {
		type = FT_AVI;
	} else if ((size >= 64) && (c[0] == 0x00) && (c[1] == 0x00) && (c[2] == 0x01) && (c[3] == 0xBA)) {
		type = FT_MPG;
	} else if ((size >= 64) && (c[4] == 0x66) && (c[5] == 0x74) && (c[6] == 0x79) && (c[7] == 0x70)) {
		type = FT_MP4;
	} else if ((size >= 20) && (c[0] == 0x46) && (c[1] == 0x4F) && (c[2] == 0x4E) && (c[3] == 0x54) && (c[4] == 0x58) && (c[5] == 0x32)) {
		type = FT_FNT;
	} else if ((size >= 20) && (c[0] == 0x46) && (c[1] == 0x4F) && (c[2] == 0x4E) && (c[3] == 0x01)) {
		type = FT_FNT;
	} else if ((size >= 32) && (c[0] == 0x3C) && (c[1] == 0x21) && (c[2] == 0x44) && (c[3] == 0x4F) && (c[10] == 0x48) && (c[11] == 0x54) && (c[12] == 0x4D) && (c[13] == 0x4C)) {
		type = FT_HTM;
	} else if ((size >= 12) && (c[0] == 0x3C) && (c[1] == 0x68) && (c[2] == 0x74) && (c[3] == 0x6D) && (c[4] == 0x6C)) {
		type = FT_HTM;
	} else if ((size >= 32) && (c[0] == 0x3C) && (c[1] == 0x21) && (c[2] == 0x44) && (c[3] == 0x4F) && (c[10] == 0x68) && (c[11] == 0x74) && (c[12] == 0x6D) && (c[13] == 0x6C)) {
		type = FT_HTM;
	} else if ((size >= 12) && (c[0] == 0x3C) && (c[1] == 0x48) && (c[2] == 0x54) && (c[3] == 0x4D) && (c[4] == 0x4C)) {
		type = FT_HTM;
	} else if ((size >= 32) && (c[0] == 0x3C) && (c[1] == 0x3F) && (c[2] == 0x78) && (c[3] == 0x6D) && (c[4] == 0x6C) && (c[5] == 0x20)) {
		type = FT_XML;
	} else if ((size >= 32) && (c[1] == 0x3C) && (c[2] == 0x3F) && (c[3] == 0x78) && (c[4] == 0x6D) && (c[5] == 0x6C) && (c[6] == 0x20)) {
		type = FT_XML;
	} else if ((size >= 80) && (c[0] == 0x00) && ((c[1] == 0x02) || (c[1] == 0x03)) && (c[2]+256*c[3]+65536*c[4] == size) && (c[5] == 0x00)) {
		type = FT_FNT;
	} else if ((size >= 32) && (c[0] == 0x4D) && (c[1] == 0x5A)) {
		type = FT_EXE;
	}
	return type;
}

int formatcheckfile(char *file)
{
	int fd;
	unsigned char buffer[512];
	unsigned int size;
	fd = nopen(file, O_RDONLY);
	if (fd < 0)
		return -1;
	size = nseek(fd, 0, SEEK_END);
	if (size > 512) size = 512;
	nseek(fd, 0, SEEK_SET);
	nread(fd, buffer, size);
	nclose(fd);
	return formatcheck(buffer, size);
}

int is_psu(unsigned char *buff, unsigned int size) 
{
	int s,i,n;
	int tmp[] = {0x10, 8, 0x20, 32, 0x60, 416, 0, 0};
	if (!buff[0x40] || !(buff[4]+buff[5])) return 0;
	for(s=0,n=0;tmp[s];s+=2){
		for(i=tmp[s];i<tmp[s]+tmp[s+1];i++) {
			if (buff[i]) n++;
		}
	}
	return !n;
}
int imagetypes[] = {FT_JPG,FT_BMP,FT_GIF,FT_PNG,FT_P2T,FT_PS1,FT_ICO,-1};
int audiotypes[] = {FT_PCM,FT_MP3,FT_AC3,FT_AAC,-1};
int videotypes[] = {FT_MPG,FT_AVI,FT_MP4,-1};
int is_image(unsigned char *buff, unsigned int size) {
	int type,i;
	type = formatcheck(buff, size);
	for(i=0;imagetypes[i]>=0;i++) if (imagetypes[i] == type) return 1;
	return 0;
}
int is_image_file(char *file) {
	int type,i;
	type = formatcheckfile(file);
	for(i=0;imagetypes[i]>=0;i++) if (imagetypes[i] == type) return 1;
	return 0;
}
int viewer(int mode, char *file, unsigned char *c, unsigned int size)
{
	int type=FT_TXT,i,info[8],bpp,tvmode;
	int dsize=size,ret=-11,dither;
	int *env=NULL;
	unsigned char *buffer=NULL, *decoded=NULL;
	tvmode = setting->tvmode;
	if (gsregs[tvmode].loaded != 1) tvmode = ITO_VMODE_AUTO-1;
	bpp = setting->screen_depth[tvmode] > 0 ? (setting->screen_depth[tvmode]-1):4-gsregs[tvmode].psm;
	dither = setting->screen_dither[tvmode] > 0 ? (setting->screen_dither[tvmode]-1):gsregs[tvmode].dither;
	if ((bpp == 2) && dither) bpp = 4;
	if (setting->txt_autodecode && ((dsize = tek_getsize(c)) >= 0)) {
		decoded = (unsigned char*)malloc(dsize);
		if (decoded != NULL) {
			if (tek_decomp(c, decoded, size)<0) {
				free(decoded); decoded=NULL;
				printf("viewer: tek auto decode failed\n");
			} else {
				printf("viewer: decoded tek compression\n");
			}
		}
	} else if (setting->txt_autodecode && ((dsize = gz_getsize(c, size)) >= 0)) {
		decoded = (unsigned char*)malloc(dsize);
		if (decoded != NULL) {
			if (gzdecode(decoded, dsize, c, size)<0) {
				free(decoded); decoded=NULL;
				printf("viewer: gzip auto decode failed\n");
			} else {
				printf("viewer: decoded gzip compression\n");
			}
		}
	}
	if (decoded == NULL) {
		dsize = size; decoded = c;
	}
	drawMsg(lang->gen_decoding);
	type = formatcheck(decoded, dsize);
	for (i=0;i<FT_TYPES;i++) {
		if (ft_type[i] == 0) {
			printf("viewer: filetype: BINARY\n"); break;
		} else if (ft_type[i] == type) {
			printf("viewer: filetype: %s\n", ft_char[i]); break;
		}
	}
	
	if (!(mode & 2) && (paddata & PAD_R2)) {
		ret = txtedit(mode | 0x0010, file, decoded, dsize);
	} else if (!(mode & 2) && (paddata & PAD_R1)) {
		ret = txtedit(mode | 0x0020, file, decoded, dsize);
	} else {
		alphablend = FALSE;
		switch(type){
			case FT_JPG:
			{
				env = malloc(16384*sizeof(int));
				if (env == NULL) { ret=-2; break; }
				env[0] = 0; env[1] = 0;
				i = info_JPEG(env, info, dsize, decoded);
				if ((info[0] != 0x0002) || (info[2] * info[3] == 0)) break;
				buffer = malloc(info[2] * info[3] * bpp);
				if (buffer == NULL) {
					if ((bpp <= 2) || ((bpp>2) && ((bpp=2)==2) && ((buffer = malloc(info[2] * info[3] * bpp))==NULL))) {
						ret=-2; break;
					}
				}
				i = decode0_JPEG(env, dsize, decoded, bpp, buffer, 0);
				ret = imgview(mode, file, buffer, info[2], info[3], bpp);
				break;
			}
			case FT_BMP:
			{
				i = info_BMP(info, decoded, dsize);
				if (bpp > ((info[1] +7)>>3)) bpp = (info[1] +7)>>3;
				if (bpp == 0) bpp++;
				if ((info[0] != 0x0001) || (info[2] * info[3] == 0)) break;
				if (bpp == 3) bpp++;
				buffer = malloc(info[2] * info[3] * bpp);
				if (buffer == NULL) {
					if ((bpp <= 2) || ((bpp>2) && ((bpp=2)==2) && ((buffer = malloc(info[2] * info[3] * bpp))==NULL))) {
						ret = -2; break;
					}
				}
				i = decode_BMP(buffer, decoded, dsize, bpp);
				ret = imgview(mode, file, buffer, info[2], info[3], bpp);
				break;
			}
			case FT_GIF:
			{
				int bpp0,b;
				if (bpp>2 && dither) bpp = 2;
				i = info_GIF(info, decoded, dsize); bpp0 = bpp;
				i = info[2] * info[3] +16;
				if ((info[0] != 0x0008) || (info[2] * info[3] == 0)) break;
				if ((info[5] == dsize) && (info[4] > 1)) {
					b = info[4] * sizeof(int) + 4;
					buffer = malloc(info[2] * info[3] * info[4] * bpp +i +b);
					if (!buffer && (bpp > 2)) {
						bpp = bpp0 = 2; buffer = malloc(info[2] * info[3] * info[4] * bpp +i +b);
					}
					if (!buffer && (bpp > 1)) {
						bpp = bpp0 = 1; buffer = malloc(info[2] * info[3] * info[4] * bpp +i +b);
					}
				}
				if (buffer == NULL) buffer = malloc(info[2] * info[3] * bpp);
				if (buffer == NULL) { ret = -2; break; }
				i = decode_GIF(buffer, decoded, dsize, bpp0);
				ret = imgview(mode|(8*((bpp0>>8)!=0)), file, buffer, info[2], info[3], bpp0);
				break;
			}
			case FT_P2T:
			{
				i = info_PS2ICO(info, decoded, dsize);
				if ((info[0] != 0x7009) || (info[2] * info[3] == 0)) break;
				buffer = malloc(info[2] * info[3] * 2);
				if (buffer == NULL) { ret = -2; break; }
				i = decode_PS2ICO(buffer, decoded, dsize, 2);
				ret = imgview(mode, file, buffer, info[2], info[3], 2);
				break;
			}
			case FT_PS1:
			{
				i = info_PS1ICO(info, decoded, dsize); bpp = 1;
				if ((info[0] != 0x700A) || (info[2] * info[3] * info[4] == 0)) break;
				buffer = (char*)malloc(info[2] * info[3] * info[4] + info[4] * sizeof(int));
				if (buffer == NULL) { ret = -2; break; }
				if (info[4] > 1) bpp |= info[4] << 8;
				i = decode_PS1ICO(buffer, decoded, dsize, bpp);
				ret = imgview(mode|8, file, buffer, info[2], info[3], bpp);
				break;
			}
			case FT_PNG:
			{
				int bpp0,b=0;
				i = info_PNG(info, decoded, dsize);
				if (info[5] & 1) bpp = 1;
				else if (info[5] & 4) bpp = bpp;
				else if (info[5] & 2) bpp = bpp;
				else bpp = 1;
				if ((info[0] != 0x000b) || (info[2] * info[3] == 0)) break;
				if (bpp == 3) bpp++;
				bpp0 = bpp; b = info[7] +32;
				if (info[6]) {
					i = info[2] * info[3] * bpp * 7; buffer = malloc(i +b);
					if (buffer != NULL) bpp0 |= 0x700;
				} else buffer = NULL;
				if (buffer == NULL) {
					i = info[2] * info[3] * bpp; buffer = malloc(i +b);
				}
				if (buffer == NULL) {
					if ((bpp <= 2) || ((bpp>2) && ((bpp=2)==2) && ((buffer = malloc((i=info[2] * info[3] * bpp)+b))==NULL))) {
						ret = -2; break;
					}
					bpp0 = bpp;
				}
				if (info[5] & 4) alphablend = TRUE;
				i = decode_PNG(buffer, decoded, dsize, bpp0);
				ret = imgview(mode, file, buffer, info[2], info[3], bpp0);
				break;
			}
		}
	}
	if (env != NULL) free(env);
	if (buffer != NULL) free(buffer);
	if ((mode & 2) && (ret == -11)) ret = -1;
	if (ret == -11) ret = txtedit(mode, file, decoded, dsize);
	if ((decoded != NULL) && (decoded != c)) free(decoded);
	return ret;
}

static int memoryerror=0;

uint64 pget(unsigned char *buffer, int x, int y, int w, int h, int bpp)
{
	unsigned char *c; unsigned short *i;
	if ((x < 0) || (x >= w) || (y < 0) || (y >= h)) return 0;
	c = buffer + (y*w+x)*(bpp & 15);
	switch(bpp) {
		case 1: case 17: return activeclut[*c];
		case 2: i = (short *) c; return ((uint64) (i[0] & 0x7C00) << 9)|((uint64) (i[0] & 0x03E0) << 6)|((uint64) (i[0] & 0x001F) << 3);
		case 3: case 19: case 4: case 20: return *(int *)c;
		case 18: i = (short *) c; return ((uint64) (i[0] & 0x7C00) << 9)|((uint64) (i[0] & 0x03E0) << 6)|((uint64) (i[0] & 0x001F) << 3)|((uint64) (i[0] & 0x8000) << 16);
	}
	return 0;
}
int imgview(int mode, char *file, unsigned char *buffer0, int w, int h, int bppf)
{
	int redraw=framebuffers,redi=0,bpp,bppb,ani,oq,q,cls=0;
	int tl,tt,tw,th,x,y,ff,fm;
	int vl,vt,vw,vh;
	int dl,dt,dw,dh;
	int k,gx,gy,gz;
	int tmpx[2048], tmpy[1088], *wait, owait, await;
	uint64 oldcount=0; static int bilinear=0;
	char msg0[MAX_PATH], msg1[MAX_PATH];
	unsigned char *buffer=NULL,*tmpbuf=NULL;
	double mx,my,dx,dy,gw,gh,pw;
	int cx,cy,ox,oy,cz,oz,ret;
	int vmode,dither,tbb,oresizer=!resizer;
	vmode = setting->tvmode;
	if (!gsregs[vmode].loaded) vmode = (ITO_VMODE_AUTO)-1;
	dither=setting->screen_dither[vmode] > 0 ? (setting->screen_dither[vmode]-1):gsregs[vmode].dither;
	strcpy(msg0, file);
	ani = bppf >> 8; bpp = bppf & 0x00ff; tbb = bpp; bppb = bpp | (alphablend << 4); fm = ffmode != 0; q = ani -1;
	wait = NULL; owait = await = 0;
	if (ani) {
		buffer = buffer0 + w * h * bpp * q;
		if (wait && aniauto) { buffer = buffer0; owait = wait[0]; q = 0; }
		await = (2000000 / SCANRATE +1) >> 1; oldcount = totalcount;
	}
	if (!buffer) buffer = buffer0; oq = q; ret = 0;
	gz = setting->tvmode; if (gsregs[gz].loaded != 1) gz = ITO_VMODE_AUTO-1;
	gx = (gsregs[gz].magx +1)<<2; gy = (gsregs[gz].magy +1)<<2;
	if (ffmode & 2) { pw = 1.0; gx = 4; gy = 4; }
	else pw = 1.0;
	cx = cy = cz = 0; oz = 1;
	ox = cx; oy = cy;
	static int entered=0; if (!entered) padEnterPressMode(entered++, 0);
	
	while(1){
		if(readpad()){
			if (new_pad & PAD_TRIANGLE) break;
			if (new_pad & PAD_CROSS) break;
			if (new_pad & PAD_SELECT) break;
			if (new_pad & PAD_CIRCLE) {
				redraw = framebuffers; fullscreen = !fullscreen;
				clrScr(setting->color[COLOR_BACKGROUND]); itoGsFinish();
			}
			if (new_pad & PAD_SQUARE) { redraw = fieldbuffers; resizer = (resizer +1) % 2; }
			if (!(ffmode & 1) && (new_pad & PAD_R2)) { redraw = fieldbuffers; bilinear ^= 1; }
			if (new_pad & PAD_START) { aniauto ^= 1; }
		}
		
		if (redraw) {
			if (!redi || bppb&16) clrScr(setting->color[COLOR_BACKGROUND]);
			if (fullscreen) { vl = 0; vt = 0; vw = SCREEN_WIDTH; vh = SCREEN_HEIGHT; }
			else { vl = 0; vt = SCREEN_MARGIN+FONT_HEIGHT*2.5+1; vw = SCREEN_WIDTH; vh = MAX_ROWS*FONT_HEIGHT+FONT_HEIGHT-1; }
			
			if (resizer) {
				dx = 1.0; dy = 1.0; tw = w; th = h;
			} else {
				dx = dy = 1.0; tw = w; th = h;
			}
			dl = (vw - tw) / 2; dt = (vh - th) / 2;
			dw = (tw > vw) ? vw : tw; dh = (th > vh) ? vh : th;
			
			for (y=0; y<dh; y++) {
				for (x=0; x<dw; x++) {
					itoPoint(pget(buffer, x+cx, y+cy, w, h, bppb), x+dl, y+dt, 0);
				}
			}
			
			drawScr();
			redraw--;
			if (redi && !redraw) redi=0;
		} else {
			// A CORREÇÃO ANTI-FLICKER FOI INJETADA EXATAMENTE AQUI:
			itoNoVSync(); 
			itoVSync();
		}
	}
	if (tmpbuf != NULL) free(tmpbuf);
	nobgmpos = 0;
	return ret;
}

int set_viewerconfig(int *data) {
	int i=0; linenum = data[i++]; tabmode = data[i++]; tabdisp = nldisp = data[i++];
	fullscreen = data[i++]; wordwrap = data[i++]; resizer = !data[i++]; aniauto = data[i++]; imgpos = data[i++];
	return i;
}
