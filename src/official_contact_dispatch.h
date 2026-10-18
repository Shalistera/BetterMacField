/* Dispatch partition from ContactShadowPassConstructor 1.6.3, RVA 0xce92c54.
   This schedules the unchanged RayTracingV2 kernel around the projected light. */
#include <math.h>

typedef struct { int x,y,z,offsetX,offsetY; } EFContactDispatch;

static int official_contact_dispatch(float lightX,float lightY,int width,int height,EFContactDispatch out[8]) {
    int left=-(int)floorf(lightX+0.5f),top=-(int)floorf(lightY+0.5f);
    int right=width+left,bottom=height+top,count=0;
    for(int quadrant=0;quadrant<4;quadrant++){
        bool diagonal=quadrant==1 || quadrant==2;
        int x0=MAX(0,(quadrant&1) ? left : -right)/64;
        int y0=MAX(0,quadrant>1 ? -bottom : top)/64;
        int x1=MAX(0,((quadrant&1) ? right : -left)+(diagonal ? 127 : 63))/64;
        int y1=MAX(0,(quadrant>1 ? -top : bottom)+(diagonal ? 63 : 127))/64;
        if(x1<=x0 || y1<=y0)continue;
        int index=count++;
        out[index]=(EFContactDispatch){64,x1-x0,y1-y0,
            ((quadrant&1) ? x0 : -x1)+(quadrant>1),
            (quadrant>1 ? -y1 : y0)+(quadrant==1 || quadrant==3)};
        int boundary=quadrant==0 ? bottom+left : quadrant==1 ? right-bottom :
            quadrant==2 ? top-left : -(right+top);
        int split=(boundary+63)/64;
        if(boundary+63>=64){
            EFContactDispatch *a=&out[index],*b=&out[count++];*b=*a;
            switch(quadrant){
                case 0: b->z=MIN(a->z,split);a->z-=b->z;b->offsetX--;b->offsetY=a->offsetY+a->z;b->y++;break;
                case 1: b->y=MIN(a->y,split);a->y-=b->y;b->offsetX=a->offsetX+a->y;b->z++;break;
                case 2: b->y=MIN(a->y,split);a->y-=b->y;a->offsetX+=b->y;b->z++;b->offsetY--;break;
                case 3: b->z=MIN(a->z,split);a->z-=b->z;a->offsetY+=b->z;b->y++;break;
            }
            if(b->y<1 || b->z<1)count--;
            if(a->y<1 || a->z<1){count--;out[index]=out[count];}
        }
    }
    for(int i=0;i<count;i++){out[i].offsetX*=64;out[i].offsetY*=64;}
    return count;
}
