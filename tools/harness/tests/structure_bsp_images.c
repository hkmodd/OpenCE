/*
STRUCTURE_BSP_IMAGES.C

The bsp images module taken whole from the port (its budget scaled to four pages); the map's bsps are buffers.
*/

#include "harness.h"

static long outstanding;
static boolean allocation_fails;
static void *fixture_malloc(size_t size) { if(allocation_fails) return NULL; ++outstanding; return malloc(size); }
static void *fixture_calloc(size_t count, size_t size) { if(allocation_fails) return NULL; ++outstanding; return calloc(count,size); }
static void fixture_free(void *data) { CHECK(data!=NULL,"free of NULL"); --outstanding; free(data); }
#define malloc fixture_malloc
#define calloc fixture_calloc
#define free fixture_free
#include "under_test.inc"
#undef malloc
#undef calloc
#undef free

static long disposed;
static void *last_disposed;
static void count_dispose(void *data) { ++disposed; last_disposed=data; }

/* the region a map's bsps are read to, and a second one */
static unsigned char region[16384], other[16384];

static void fill(unsigned char *bytes, long size, int seed) { long i; for(i=0;i<size;++i) bytes[i]=(unsigned char)(seed+i*7); }
static boolean same(unsigned char const *bytes, long size, int seed) { long i; for(i=0;i<size;++i) if(bytes[i]!=(unsigned char)(seed+i*7)) return FALSE; return TRUE; }

int main(int argc, char **argv)
{
    char const *case_name=argc==2?argv[1]:"";
    long i;
    CASE("keep-restore")
    {
        fill(region,1024,1); structure_bsp_image_keep(5,512,1024,region);
        CHECK(outstanding==1,"one image");
        memset(region,0,sizeof(region));
        CHECK(!structure_bsp_image_restore(6,512,1024,region),"another tag");
        CHECK(!structure_bsp_image_restore(5,1024,1024,region),"another offset");
        CHECK(!structure_bsp_image_restore(5,512,1536,region),"another size");
        CHECK(!structure_bsp_image_restore(5,512,1024,other),"another address");
        CHECK(region[1]==0 && !structure_bsp_image_attachment(),"no copy, no image");
        CHECK(structure_bsp_image_restore(5,512,1024,region) && same(region,1024,1),"copied back as kept");
        CHECK(region[1024]==0,"no more than its size");
        structure_bsp_images_dispose(); return 0;
    }
    CASE("keep-once")
    {
        fill(region,1024,1); structure_bsp_image_keep(5,512,1024,region);
        fill(region,1024,2); structure_bsp_image_keep(5,512,1024,region);
        CHECK(outstanding==1,"kept once");
        memset(region,0,sizeof(region));
        CHECK(structure_bsp_image_restore(5,512,1024,region) && same(region,1024,1),"the first keeps its bytes");
        structure_bsp_images_dispose(); return 0;
    }
    CASE("budget")
    {
        fill(region,8192,1); structure_bsp_image_keep(1,0,8192,region);
        fill(region,8192,2); structure_bsp_image_keep(2,0,8192,region);
        fill(region,512,3); structure_bsp_image_keep(3,0,512,region);
        CHECK(outstanding==2,"over the budget: refused");
        CHECK(!structure_bsp_image_restore(3,0,512,region),"refused: read again");
        CHECK(structure_bsp_image_restore(1,0,8192,region) && same(region,8192,1),"the first kept");
        structure_bsp_image_keep(4,0,0,region); structure_bsp_image_keep(4,0,-512,region);
        CHECK(outstanding==2,"no empty image");
        structure_bsp_images_dispose(); CHECK(!outstanding,"all freed");
        fill(region,512,3); structure_bsp_image_keep(3,0,512,region);
        CHECK(outstanding==1,"the budget is the map's"); structure_bsp_images_dispose(); return 0;
    }
    CASE("count")
    {
        for(i=0;i<20;++i) { fill(region,16,(int)i); structure_bsp_image_keep(i,(long)i*16,16,region); }
        CHECK(outstanding==16,"a scenario's bsps at most");
        CHECK(!structure_bsp_image_restore(16,256,16,region) && structure_bsp_image_restore(15,240,16,region) && same(region,16,15),"the first sixteen");
        structure_bsp_images_dispose(); return 0;
    }
    CASE("attach")
    {
        long *data, *again;
        fill(region,512,1); structure_bsp_image_keep(1,0,512,region);
        data=structure_bsp_image_attach(64,count_dispose);
        CHECK(data && data[0]==0 && data[15]==0 && structure_bsp_image_attachment()==data,"zeroed, attached");
        data[0]=7;
        again=structure_bsp_image_attach(64,count_dispose);
        CHECK(again && again!=data && disposed==1 && last_disposed==data && outstanding==2,"in place of what was, disposed and freed");
        CHECK(!structure_bsp_image_attach(0,count_dispose) && structure_bsp_image_attachment()==again && disposed==1,"nothing empty attached");
        structure_bsp_images_dispose();
        CHECK(disposed==2 && last_disposed==again && !outstanding && !structure_bsp_image_attachment(),"disposed with the image"); return 0;
    }
    CASE("attach-follows-load")
    {
        void *first, *second;
        fill(region,512,1); structure_bsp_image_keep(1,0,512,region); first=structure_bsp_image_attach(8,count_dispose);
        fill(region,512,2); structure_bsp_image_keep(2,0,512,region); second=structure_bsp_image_attach(8,count_dispose);
        CHECK(first && second && first!=second,"one per image");
        CHECK(structure_bsp_image_restore(1,0,512,region) && structure_bsp_image_attachment()==first,"the loaded bsp's");
        CHECK(!structure_bsp_image_restore(3,0,512,region) && !structure_bsp_image_attachment() && !structure_bsp_image_attach(8,count_dispose),"a bsp not kept has none");
        CHECK(structure_bsp_image_restore(2,0,512,region) && structure_bsp_image_attachment()==second,"the other's");
        fill(region,16384,3); structure_bsp_image_keep(3,0,16384,region);
        CHECK(outstanding==4 && !structure_bsp_image_attachment() && !structure_bsp_image_attach(8,count_dispose),"refused: no image to attach to");
        CHECK(disposed==0,"nothing replaced");
        structure_bsp_images_dispose(); return 0;
    }
    CASE("dispose")
    {
        for(i=0;i<3;++i) { fill(region,512,(int)i); structure_bsp_image_keep(i,0,512,region); CHECK(structure_bsp_image_attach(8,count_dispose)!=NULL,"attached"); }
        structure_bsp_images_dispose();
        CHECK(!outstanding && disposed==3,"bytes and attachments go");
        CHECK(!structure_bsp_image_restore(0,0,512,region) && !structure_bsp_image_attachment(),"nothing kept");
        structure_bsp_images_dispose(); CHECK(!outstanding && disposed==3,"twice is harmless"); return 0;
    }
    CASE("kept-flag")
    {
        CHECK(!structure_bsp_images_kept(),"not before");
        structure_bsp_images_keep_begun(); CHECK(structure_bsp_images_kept(),"kept once begun, even with nothing kept");
        structure_bsp_images_dispose(); CHECK(!structure_bsp_images_kept(),"again for the next map"); return 0;
    }
    CASE("keep-allocation-failure")
    {
        fill(region,512,1); allocation_fails=TRUE; structure_bsp_image_keep(1,0,512,region); allocation_fails=FALSE;
        CHECK(!outstanding && !structure_bsp_image_restore(1,0,512,region) && !structure_bsp_image_attach(8,count_dispose),"not kept");
        structure_bsp_image_keep(1,0,512,region);
        CHECK(outstanding==1,"kept when it can be"); structure_bsp_images_dispose(); return 0;
    }
    CASE("attach-allocation-failure")
    {
        void *data;
        fill(region,512,1); structure_bsp_image_keep(1,0,512,region); data=structure_bsp_image_attach(8,count_dispose);
        allocation_fails=TRUE;
        CHECK(!structure_bsp_image_attach(8,count_dispose) && structure_bsp_image_attachment()==data && !disposed,"what was attached stays");
        allocation_fails=FALSE; structure_bsp_images_dispose(); CHECK(!outstanding,"freed"); return 0;
    }
    CASE("gles")
    {
        fill(region,512,1); structure_bsp_image_keep(1,0,512,region);
        CHECK(!outstanding && !structure_bsp_image_restore(1,0,512,region),"the GLES builds keep nothing");
        CHECK(structure_bsp_images_kept(),"nothing can be kept: not read ahead"); return 0;
    }
    fprintf(stderr,"unknown case %s\n",case_name);
    return 2;
}
