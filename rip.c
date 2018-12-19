#include "rip.h"
TRtEntry *g_pstRouteEntry = NULL;//Route Table
TRipPkt *ripSendReqPkt = NULL;
TRipPkt *ripSendUpdPkt = NULL;
TRipPkt *ripReceivePkt = NULL;
TRipPkt *ripResponsPkt = NULL;
struct in_addr pcLocalAddr[10];//存储本地接口ip地址
char *pcLocalName[10] = {};//存储本地接口的接口名
struct in_addr pcLocalMask[10];
int interCount = 0;

int directConnect(struct in_addr a, struct in_addr b, struct in_addr m)
{
    return (a.s_addr & m.s_addr) == (b.s_addr & m.s_addr);
}


void requestpkt_Encapsulate()
{
    printf("%s\n", "Encapsulate the request package");
    //封装请求包  command =1,version =2,family =0,metric =16
    ripSendReqPkt->ucCommand = RIP_REQUEST;
    ripSendReqPkt->ucVersion = RIP_VERSION;
    ripSendReqPkt->usZero = 0;
    ripSendReqPkt->RipEntries[0].usFamily = 0;
    ripSendReqPkt->RipEntries[0].uiMetric = htonl(RIP_INFINITY);
    ripSendReqPkt->ripEntryCount = 1;
}


/*****************************************************
*Func Name:    rippacket_Receibuhaobuh********************/
void rippacket_Receive()
{
    int receivefd;
    struct sockaddr_in local_addr;
    //接收ip设置

    memset(&local_addr, 0, sizeof(struct sockaddr_in));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(RIP_PORT);  //注意网络序转换

    //创建并绑定socket

    receivefd = socket(AF_INET, SOCK_DGRAM, 0);
    if(receivefd < 0)
    {
        perror("create socket fail!\n");
        exit(-1);
    }

    //防止绑定地址冲突，仅供参考
    //设置地址重用
    int  iReUseddr = 1;
    if (setsockopt(receivefd,SOL_SOCKET ,SO_REUSEADDR,(const char*)&iReUseddr,sizeof(iReUseddr))<0)
    {
        perror("reuse addr error\n");
        return ;
    }
    //设置端口重用
    int  iReUsePort = 1;
    if (setsockopt(receivefd,SOL_SOCKET ,SO_REUSEPORT,(const char*)&iReUsePort,sizeof(iReUsePort))<0)
    {
        perror("reuse port error\n");
        return ;
    }

    if(bind(receivefd,(struct sockaddr*)&local_addr, sizeof(struct sockaddr_in))<0)
    {
        perror("bind error\n");
        exit(-1);
    }

    //把本地地址加入到组播中
    for(int i = 0; i < interCount; i ++)
    {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr=inet_addr(RIP_GROUP);
        mreq.imr_interface=pcLocalAddr[i];
        /* 把本机加入组播地址，即本机网卡作为组播成员，只有加入组才能收到组播消息 */
        if (setsockopt(receivefd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,sizeof (struct ip_mreq)) == -1)
        {
            perror ("join in multicast error\n");
            exit (-1);
        }
    }

    //防止组播回环的参考代码

    //0 禁止回环  1开启回环
    int loop = 0;
    int err = setsockopt(receivefd,IPPROTO_IP, IP_MULTICAST_LOOP,&loop, sizeof(loop));
    if(err < 0)
    {
        perror("setsockopt():IP_MULTICAST_LOOP");
    }

    char buff[1500];
    while (1)
    {
        memset(buff, 0, sizeof(buff));
        size_t addrLength = sizeof(struct sockaddr_in);
        memset(&local_addr, 0, addrLength);
        //接收rip报文   存储接收源ip地址
        int recvLength = 0;
        if((recvLength= (int) recvfrom(receivefd, buff, sizeof(buff), 0, (struct sockaddr*)&local_addr, &addrLength)) < 0)
        {
                perror("recv error\n");
        }
        printf("Start receive...\n");
        printf("    received %d sized ripPacket from %s\n", recvLength, inet_ntoa(local_addr.sin_addr));
        if(recvLength > RIP_MAX_PACKET)
        {
            printf("What the hell? It's too large\n");
            continue;
        }
        /*
        int isFromMe = 0;
        for(int i = 0;i < interCount; i++)
            if(local_addr.sin_addr.s_addr == pcLocalAddr[i].s_addr)
            {
                isFromMe = 1;
            }
        if (isFromMe)continue;
         */
        //判断command类型，request 或 response
        ripReceivePkt = (TRipPkt *)buff;
        ripReceivePkt->ripEntryCount = (recvLength-4)/20;
        printf("    command is %d, rip version is %d\n",ripReceivePkt->ucCommand, ripReceivePkt->ucVersion);
        //接收到的信息存储到全局变量里，方便request_Handle和response_Handle处理
        if(ripReceivePkt->ucCommand == RIP_RESPONSE)
        {
            response_Handle(local_addr.sin_addr);
        }else if(ripReceivePkt->ucCommand == RIP_REQUEST && ripReceivePkt->RipEntries[0].usFamily == 0 && htonl(ripReceivePkt->RipEntries[0].uiMetric) == RIP_INFINITY)
        {
            request_Handle(local_addr.sin_addr);
        }else
        {
            printf("What the hell\n");
        }
    }
}


/*****************************************************
*Func Name:    rippacket_Send  
*Description:  向接收源发送响应报文
*Input:        
*	  1.stSourceIp    ：接收源的ip地址，用于发送目的ip设置
*Output: 
*
*Ret  ：
*
*******************************************************/
void rippacket_Send(struct in_addr stSourceIp, struct in_addr pcLocalAddr)
{
    printf("            Send response packet from %s ", inet_ntoa(pcLocalAddr));
    printf("to %s\n", inet_ntoa(stSourceIp));
    int sendfd;
    struct sockaddr_in local_addr, peer_addr;
    //本地ip设置
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr = pcLocalAddr;
    local_addr.sin_port = htons(RIP_PORT);
    //发送目的ip设置
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr = stSourceIp;
    peer_addr.sin_port = htons(RIP_PORT);
    //防止绑定地址冲突，仅供参考
    sendfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sendfd < 0)
    {
        perror("create socket fail!\n");
        exit(-1);
    }
    //设置地址重用
    int  iReUseddr = 1;
    if (setsockopt(sendfd,SOL_SOCKET ,SO_REUSEADDR,(const char*)&iReUseddr,sizeof(iReUseddr))<0)
    {
        perror("setsockopt\n");
        return ;
    }
    //设置端口重用
    int  iReUsePort = 1;
    if (setsockopt(sendfd,SOL_SOCKET ,SO_REUSEPORT,(const char*)&iReUsePort,sizeof(iReUsePort))<0)
    {
        perror("setsockopt\n");
        return ;
    }
    //把本地地址加入到组播中//←_← what the hell

    //创建并绑定socket

    if (bind(sendfd, (struct sockaddr*)&local_addr, sizeof(local_addr))<0)
    {
        perror("bind error\n");
        exit(-1);
    }
    //发送
    short dataLength = (short)(4 + ripResponsPkt->ripEntryCount * 20);
    if (sendto(sendfd, ripResponsPkt, (size_t)dataLength, 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr))<0)
    {
        perror("send request error\n");
        exit(-1);
    }

    close(sendfd);
}

/*****************************************************
*Func Name:    rippacket_Multicast  
*Description:  组播请求报文
*Input:        
*	  1.pcLocalAddr   ：本地ip地址
*Output: 
*
*Ret  ：
*
*******************************************************/
void rippacket_Multicast(struct in_addr pcLocalAddr, struct RipPacket * ripPacket)
{
    printf("    Multicast request packet from %s\n", inet_ntoa(pcLocalAddr));
    int sendfd;
    struct sockaddr_in local_addr, peer_addr;
    //本地ip设置
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr = pcLocalAddr;
    local_addr.sin_port = htons(RIP_PORT);
    //目的ip设置
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr.s_addr = inet_addr(RIP_GROUP);
    peer_addr.sin_port = htons(RIP_PORT);

    //创建并绑定socket
    sendfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sendfd < 0)
    {
        perror("create socket fail!\n");
        exit(-1);
    }
    //防止绑定地址冲突，仅供参考
    //设置地址重用
    int  iReUseddr = 1;
    if (setsockopt(sendfd,SOL_SOCKET ,SO_REUSEADDR,(const char*)&iReUseddr,sizeof(iReUseddr))<0)
    {
        perror("reuse addr error\n");
        exit(-1);
    }
    //设置端口重用
    int  iReUsePort = 1;
    if (setsockopt(sendfd,SOL_SOCKET ,SO_REUSEPORT,(const char*)&iReUsePort,sizeof(iReUsePort))<0)
    {
        perror("reuse port error\n");
        exit(-1);
    }
    if (bind(sendfd, (struct sockaddr*)&local_addr, sizeof(local_addr))<0)
    {
        perror("bind error\n");
        exit(-1);
    }

    //把本地地址加入到组播中
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(RIP_GROUP);
    mreq.imr_interface = pcLocalAddr;
    if (setsockopt(sendfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,sizeof (struct ip_mreq)) == -1)
    {
        perror ("join in multicast error\n");
        exit (-1);
    }

    //防止组播回环的参考代码

    //0 禁止回环  1开启回环
    int loop = 0;
    int err = setsockopt(sendfd,IPPROTO_IP, IP_MULTICAST_LOOP,&loop, sizeof(loop));
    if(err < 0)
    {
        perror("setsockopt():IP_MULTICAST_LOOP");
    }
    //

    //发送
    short dataLength = (short)(4 + ripPacket->ripEntryCount * 20);
    if (sendto(sendfd, ripPacket, (size_t)dataLength, 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr))<0)
    {
        perror("send request error\n");
        exit(-1);
    }/*
    if(ripPacket->ucCommand == RIP_REQUEST && pcLocalAddr.s_addr == inet_addr("192.168.3.1"))
    {
        size_t addrLength = sizeof(peer_addr);
        int recvLength;
        if((recvLength = (int)recvfrom(sendfd, ripPacket, 100, 0 ,(struct sockaddr*)&peer_addr, &addrLength) )< 0)
        {
            printf("fuck you!\n");
        }
        printf("    received %d sized ripPacket from %s\n", recvLength, inet_ntoa(peer_addr.sin_addr));
    }*/
-
    close(sendfd);
/*
    if (setsockopt(sendfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq,sizeof (struct ip_mreq)) == -1)
    {
        perror ("leave multicast error\n");
        exit (-1);
    }

    close(sendfd);
    */
}

/*****************************************************
*Func Name:    request_Handle  
*Description:  响应request报文
*Input:        
*	  1.stSourceIp   ：接收源的ip地址
*Output: 
*
*Ret  ：
*
*******************************************************/
void request_Handle(struct in_addr stSourceIp)
{
    printf("        I need to response someone\n");
    //处理request报文
    //遵循水平分裂算法
    //回送response报文，command置为RIP_RESPONSE
    TRtEntry *now = g_pstRouteEntry;
    while(now != NULL)
    {
        ripResponsPkt->ucCommand = RIP_RESPONSE;
        ripResponsPkt->ucVersion = RIP_VERSION;
        ripResponsPkt->usZero = 0;
        ripResponsPkt->ripEntryCount = 0;
        while(now != NULL && ripResponsPkt->ripEntryCount < RIP_MAX_ENTRY)
        {
            ripResponsPkt->RipEntries[ripResponsPkt->ripEntryCount].stAddr = now->stIpPrefix;
            ripResponsPkt->RipEntries[ripResponsPkt->ripEntryCount].stNexthop = now->stNexthop;
            ripResponsPkt->RipEntries[ripResponsPkt->ripEntryCount].uiMetric = now->uiMetric;
            ripResponsPkt->RipEntries[ripResponsPkt->ripEntryCount].stPrefixLen = now->uiPrefixLen;
            ripResponsPkt->RipEntries[ripResponsPkt->ripEntryCount].usFamily = htons(2);
            ripResponsPkt->RipEntries[ripResponsPkt->ripEntryCount].usTag = 0;
            ripResponsPkt->ripEntryCount ++;
            now = now->pstNext;
        }
        if(ripResponsPkt->ripEntryCount)
        {
            for(int i = 0; i < interCount ; i ++)if(directConnect(pcLocalAddr[i], stSourceIp, pcLocalMask[i]))
            {
                rippacket_Send(stSourceIp, pcLocalAddr[i]);
            }
        }
    }
}

/*****************************************************
*Func Name:    response_Handle  
*Description:  响应response报文
*Input:        
*	  1.stSourceIp   ：接收源的ip地址
*Output: 
*
*Ret  ：
*
*******************************************************/
void response_Handle(struct in_addr stSourceIp)
{
    printf("        Update my Rip route table!!!\n");
    struct timeval tv;
    gettimeofday(&tv,NULL);
    int needUpdate = 0;
    for(int i = 0; i < ripReceivePkt->ripEntryCount; i ++)
    {
        int find = 0;
        TRtEntry *now = g_pstRouteEntry;
        while(now != NULL)
        {
            if(now->stIpPrefix.s_addr == ripReceivePkt->RipEntries[i].stAddr.s_addr)
            {
                find = 1;
                uint32_t my = htonl(now->uiMetric), his = htonl(ripReceivePkt->RipEntries[i].uiMetric) + 1;
                if(his > RIP_INFINITY)// delete this route in Route Table
                {
                    now->isValid = ROUTE_NOTVALID;
                    needUpdate = 1;
                }else if(now->stNexthop.s_addr == ripReceivePkt->RipEntries[i].stNexthop.s_addr)
                {
                    if(my != his)
                    {
                        needUpdate = 1;
                    }
                    now->uiMetric = ntohl(his);
                    now->isValid = ROUTE_VALID;
                    if(his == 16)// not useful
                    {
                        now->isValid = ROUTE_NOTVALID;
                    }
                    now->lastUpdataTime = tv.tv_sec;
                }else if(my >= his)
                {
                    if(my != his || now->stNexthop.s_addr != stSourceIp.s_addr)
                    {
                        needUpdate = 1;
                    }
                    now->uiMetric = ntohl(his);
                    now->stNexthop = stSourceIp;
                    now->isValid = ROUTE_VALID;
                    if(his == 16)//not useful
                    {
                        now->isValid = ROUTE_NOTVALID;
                    }
                    now->lastUpdataTime = tv.tv_sec;
                }
            }
            now = now->pstNext;
        }
        if(!find && ripReceivePkt->RipEntries[i].uiMetric != htonl(RIP_INFINITY))
        {
            needUpdate = 1;
            now = (TRtEntry *) malloc(sizeof(TRtEntry));
            now->stIpPrefix = ripReceivePkt->RipEntries[i].stAddr;
            now->stNexthop = stSourceIp;
            now->uiPrefixLen = ripReceivePkt->RipEntries[i].stPrefixLen;
            now->uiMetric = ntohl(htonl(ripReceivePkt->RipEntries[i].uiMetric) + 1);
            now->isValid = ROUTE_VALID;
            now->lastUpdataTime = tv.tv_sec;
            if(htonl(now->uiMetric) == 16)//not useful
            {
                now->isValid = ROUTE_NOTVALID;
            }
            route_SendForward(AddRoute,now);
            now->pstNext = g_pstRouteEntry;
            g_pstRouteEntry = now;
        }
    }
    TRtEntry *now = g_pstRouteEntry;
    while(now != NULL)
    {
        printf("            ipPrefix=%16s ", inet_ntoa(now->stIpPrefix));
        printf("nextHop=%16s", inet_ntoa(now->stNexthop));
        printf(" prefixLen=%16s metric=%2d\n", inet_ntoa(now->uiPrefixLen), htonl(now->uiMetric));
        now=now->pstNext;
    }
    if(needUpdate)
    {
        printf("Update triggered!!! I need to tell my neighbours!!!\n");
        send_update_to_neighbour();

    }
}

/*****************************************************
*Func Name:    route_SendForward  
*Description:  响应response报文
*Input:        
*	  1.uiCmd        ：插入命令
*	  2.pstRtEntry   ：路由信息
*Output: 
*
*Ret  ：
*
*******************************************************/
void route_SendForward(unsigned int uiCmd, TRtEntry *pstRtEntry)
{
    //建立tcp短连接，发送插入、删除路由表项信息到转发引擎
    printf("Something need to notify my forward engine:");
    if(uiCmd == AddRoute)
    {
        printf("    Add a new route!");
    }else
    {
        printf("    Delelte a old route!");
    }
    printf("        ipPrefix=%16s ",inet_ntoa(pstRtEntry->stIpPrefix));
    printf("nextHop=%16s", inet_ntoa(pstRtEntry->stNexthop));
    printf(" prefixLen=%16s\n", inet_ntoa(pstRtEntry->uiPrefixLen));
}

void rippacket_Update(struct in_addr pcLocalAddr)
{
    //遍历rip路由表，封装更新报文
    TRtEntry *now = g_pstRouteEntry;
    struct timeval tv;
    gettimeofday(&tv,NULL);
    while(now != NULL)
    {
        ripSendUpdPkt->ucCommand = RIP_RESPONSE;
        ripSendUpdPkt->ucVersion = RIP_VERSION;
        ripSendUpdPkt->usZero = 0;
        ripSendUpdPkt->ripEntryCount = 0;
        while(now != NULL && ripSendUpdPkt->ripEntryCount < RIP_MAX_ENTRY)
        {
            if(!directConnect(now->stIpPrefix, pcLocalAddr, now->uiPrefixLen))
            {
                if(tv.tv_sec - now->lastUpdataTime > ROUTE_MAX_INTERVAL && now->uiMetric != htonl(1))
                {
                    printf("%d %d\n", tv.tv_sec, now->lastUpdataTime);
                    now->isValid = ROUTE_NOTVALID;
                }
                ripSendUpdPkt->RipEntries[ripSendUpdPkt->ripEntryCount].stAddr = now->stIpPrefix;
                ripSendUpdPkt->RipEntries[ripSendUpdPkt->ripEntryCount].stNexthop = now->stNexthop;
                ripSendUpdPkt->RipEntries[ripSendUpdPkt->ripEntryCount].uiMetric = (now->isValid == ROUTE_NOTVALID || directConnect(pcLocalAddr, now->stNexthop, now->uiPrefixLen))?htonl(RIP_INFINITY):now->uiMetric;//poison reverse
                ripSendUpdPkt->RipEntries[ripSendUpdPkt->ripEntryCount].stPrefixLen = now->uiPrefixLen;
                ripSendUpdPkt->RipEntries[ripSendUpdPkt->ripEntryCount].usFamily = htons(2);
                ripSendUpdPkt->RipEntries[ripSendUpdPkt->ripEntryCount].usTag = 0;
                ripSendUpdPkt->ripEntryCount ++;
            }
            now = now->pstNext;
        }
        if(ripSendUpdPkt->ripEntryCount)
        {
            rippacket_Multicast(pcLocalAddr, ripSendUpdPkt);
        }
    }
    //注意水平分裂算法
}
void routeTableDelete()//Delete not valid item
{
    TRtEntry *now = g_pstRouteEntry;
    while(now->isValid == ROUTE_NOTVALID)
    {
        TRtEntry *tmp = g_pstRouteEntry;
        route_SendForward(DelRoute,g_pstRouteEntry);
        g_pstRouteEntry = now->pstNext;
        now = now->pstNext;
        free(tmp);
    }
    while(now != NULL)
    {
        while(now->pstNext != NULL && now->pstNext->isValid == ROUTE_NOTVALID)
        {
            route_SendForward(DelRoute,now->pstNext);
            TRtEntry *tmp = now->pstNext;
            now->pstNext=now->pstNext->pstNext;
            free(tmp);
        }
        now = now->pstNext;
    }
}

void send_update_to_neighbour()
{
    for(int i = 0; i < interCount ; i++)
    {
        rippacket_Update(pcLocalAddr[i]);
    }
    routeTableDelete();
    printf("    This is my now rip table!\n");
    TRtEntry *now = g_pstRouteEntry;
    while(now != NULL)
    {
        printf("            ipPrefix=%16s ", inet_ntoa(now->stIpPrefix));
        printf("nextHop=%16s", inet_ntoa(now->stNexthop));
        printf(" prefixLen=%16s metric=%2d\n", inet_ntoa(now->uiPrefixLen), htonl(now->uiMetric));
        now=now->pstNext;
    }
}
void *update_thread(void * arg)
{
    while(1)
    {
        sleep(UPDATE_INTERVAL);
        printf("It's time to update...\n");
        send_update_to_neighbour();
    }
}

void send_Request()
{
    sleep(1);
    requestpkt_Encapsulate();
    for (int i = 0; i < interCount; i ++)
    {
        rippacket_Multicast(pcLocalAddr[i], ripSendReqPkt);
    }
}
void ripdaemon_Start()
{
    //创建更新线程，30s更新一次,向组播地址更新Update包
    pthread_t tid;
    int ret = pthread_create(&tid, NULL, update_thread, NULL);
    if (ret != 0)
    {
        perror("new thread error!\n");
        exit(-1);
    }

    //封装请求报文，并组播
    pthread_t tid2;
    int ret2 = pthread_create(&tid2, NULL, send_Request, NULL);
    if(ret2 != 0)
    {
        perror("new thread error!\n");
        exit(-1);
    }
    //接收rip报文
    rippacket_Receive();
}

void routentry_Insert()
{
    //将本地接口表添加到rip路由表里
    printf("%s\n","Add interfaces into Rip Route Table");
    struct timeval tv;
    gettimeofday(&tv,NULL);
    TRtEntry *j = g_pstRouteEntry;
    for(int i = 0; i < interCount;i ++, j=j->pstNext)
    {
        j->stIpPrefix.s_addr = pcLocalAddr[i].s_addr & pcLocalMask->s_addr;
        j->stNexthop.s_addr = inet_addr("0.0.0.0");
        j->uiPrefixLen = pcLocalMask[i];
        j->uiMetric = htonl(1);
        j->pstNext = NULL;
        j->lastUpdataTime = tv.tv_sec;
        j->isValid = ROUTE_VALID;
        route_SendForward(AddRoute,j);
        /*
        printf("    %d ipPrefix=%16s ",i, inet_ntoa(j->stIpPrefix));
        printf("nextHop=%16s", inet_ntoa(j->stNexthop));
        printf(" prefixLen=%16s metric=%2d\n", inet_ntoa(j->uiPrefixLen), htonl(j->uiMetric));
         */
        if(i < interCount - 1)
        {
            j->pstNext = (TRtEntry *) malloc(sizeof(TRtEntry));
            if(j->pstNext == NULL)
            {
                perror("g_pstRouteEntry malloc error !\n");
                exit(-1);
            }
        }
    }
}

void localinterf_GetInfo()
{
    struct ifaddrs *pstIpAddrStruct = NULL;
    struct ifaddrs *pstIpAddrStCur = NULL;
    void *pAddrPtr = NULL;
    const char *pcLo = "127.0.0.1";

    getifaddrs(&pstIpAddrStruct); //linux系统函数
    pstIpAddrStCur = pstIpAddrStruct;
    printf("%s\n", "Get local interfaces");
    int i = 0;
    while (pstIpAddrStruct != NULL)
    {
        if (pstIpAddrStruct->ifa_addr->sa_family == AF_INET)
        {

            pAddrPtr = &((struct sockaddr_in *) pstIpAddrStruct->ifa_addr)->sin_addr;
            char cAddrBuf[INET_ADDRSTRLEN];
            memset(&cAddrBuf, 0, sizeof(INET_ADDRSTRLEN));
            inet_ntop(AF_INET, pAddrPtr, cAddrBuf, INET_ADDRSTRLEN);
            if (strcmp((const char *) &cAddrBuf, pcLo) != 0)
            {
                pcLocalAddr[i] = ((struct sockaddr_in *) pstIpAddrStruct->ifa_addr)->sin_addr;
                pcLocalName[i] = (char *) malloc(sizeof(IF_NAMESIZE));
                pcLocalMask[i] = ((struct sockaddr_in *)(pstIpAddrStruct->ifa_netmask))->sin_addr;
                strcpy(pcLocalName[i], (const char *) pstIpAddrStruct->ifa_name);
                printf("    %d: addr = %16s name = %s ", i, inet_ntoa(pcLocalAddr[i]), pcLocalName[i]);
                printf("mask = %s\n",inet_ntoa(pcLocalMask[i]));
                i++;
                interCount++;
            }
        }
        pstIpAddrStruct = pstIpAddrStruct->ifa_next;
    }
    freeifaddrs(pstIpAddrStCur);//linux系统函数
}

int main(int argc, char *argv[])
{
    g_pstRouteEntry = (TRtEntry *) malloc(sizeof(TRtEntry));
    ripSendReqPkt = (struct RipPacket *) malloc(sizeof(struct RipPacket));
    ripSendUpdPkt = (struct RipPacket *) malloc(sizeof(struct RipPacket));
    ripResponsPkt = (struct RipPacket *) malloc(sizeof(struct RipPacket));
    localinterf_GetInfo();
    routentry_Insert();
    ripdaemon_Start();
    return 0;
}

