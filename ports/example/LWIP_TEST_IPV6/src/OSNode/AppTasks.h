/*********************************************                                                       
 * ECG Tasks Based on Interrupt-Driven Task Model                                                                     
 * Zhuoran Zhao, UT Austin, zhuoran@utexas.edu                                                    
 * Last update: July 2017                                                                           
 ********************************************/
#include <systemc>
#include "HCSim.h"

#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/sys.h"
#include "lwip/stats.h"
#include "lwip/inet_chksum.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include "lwip/api.h" //netconn_api libraries 



#include "netif/hcsim_if.h"



#include "arch/perf.h"
#include "lwip_ctxt.h"
#include "annotation.h"
#include "OmnetIf_pkt.h"
#include "app_utils.h"


#ifndef SC_VISION_TASK__H
#define SC_VISION_TASK__H




void tcpip_init_done(void *arg);
void recv_dats(void *arg);
void send_dats(void *arg);



class IntrDriven_Task: public sc_core::sc_module, virtual public HCSim::OS_TASK_INIT 
{
 public:
    sc_core::sc_vector< sc_core::sc_port< lwip_recv_if > > recv_port;
    sc_core::sc_vector< sc_core::sc_port< lwip_send_if > > send_port;
    sc_core::sc_port< HCSim::OSAPI > os_port;
    void* g_ctxt;

    SC_HAS_PROCESS(IntrDriven_Task);
  	IntrDriven_Task(const sc_core::sc_module_name name, 
            sc_dt::uint64 exe_cost, sc_dt::uint64 period, 
            unsigned int priority, int id, uint8_t init_core,
            sc_dt::uint64 end_sim_time, int NodeID)
    :sc_core::sc_module(name)
    {
        this->exe_cost = exe_cost;
        this->period = period;
        this->priority = priority;
        this->id = id;
        this->end_sim_time = end_sim_time;
	this->NodeID = NodeID;
        this->init_core = 1;
	recv_port.init(2);
	send_port.init(2);
        g_ctxt=new lwip_context();
	IP_ADDR6(&((lwip_context* )g_ctxt)->ipaddr,  1, 2, 3, (4 + NodeID));
	((lwip_context* )g_ctxt) -> OSmodel = (new OSModelCtxt());
	((OSModelCtxt*)(((lwip_context* )g_ctxt)->OSmodel))->NodeID = NodeID;
	((OSModelCtxt*)(((lwip_context* )g_ctxt)->OSmodel))->flag_compute=0;
	((OSModelCtxt*)(((lwip_context* )g_ctxt)->OSmodel))->os_port(this->os_port);
	((OSModelCtxt*)(((lwip_context* )g_ctxt)->OSmodel))->recv_port[0](this->recv_port[0]);
	((OSModelCtxt*)(((lwip_context* )g_ctxt)->OSmodel))->recv_port[1](this->recv_port[1]);
	((OSModelCtxt*)(((lwip_context* )g_ctxt)->OSmodel))->send_port[0](this->send_port[0]);
	((OSModelCtxt*)(((lwip_context* )g_ctxt)->OSmodel))->send_port[1](this->send_port[1]);
        SC_THREAD(run_jobs);
    }
    
    ~IntrDriven_Task() {}
    void OSTaskCreate(void)
    {
	printf("Setting up NodeID %d ............... \n", NodeID);
	((lwip_context* )g_ctxt)->NodeID = NodeID;
        os_task_id = os_port->taskCreate(sc_core::sc_gen_unique_name("intrdriven_task"), 
                                HCSim::OS_RT_APERIODIC, priority, period, exe_cost, 
                                HCSim::DEFAULT_TS, HCSim::ALL_CORES, init_core);
    }

 private:
    int id;
    uint8_t init_core;
    sc_dt::uint64 exe_cost;
    sc_dt::uint64 period;
    unsigned int priority;
    sc_dt::uint64 end_sim_time;
    int os_task_id;
    int NodeID;

    void run_jobs(void)
    {
	os_port->taskActivate(os_task_id);
	os_port->timeWait(0, os_task_id);
	os_port->syncGlobalTime(os_task_id);
        taskManager.registerTask( (OSModelCtxt*)(((lwip_context*)(g_ctxt))->OSmodel ), g_ctxt, os_task_id, sc_core::sc_get_current_process_handle());
	//netif_init();
	tcpip_init(tcpip_init_done, g_ctxt);
	printf("Applications started, NodeID is %d %d\n", ((lwip_context* )g_ctxt)->NodeID, taskManager.getTaskID(sc_core::sc_get_current_process_handle()));
	printf("TCP/IP initialized.\n");
	sys_thread_new("send_dats", send_dats, ((lwip_context* )g_ctxt), DEFAULT_THREAD_STACKSIZE, init_core);
	recv_dats(g_ctxt);
        os_port->taskTerminate(os_task_id);
    }
};



void tcpip_init_done(void *arg)
{
  lwip_context* ctxt = (lwip_context*)arg;


//#if LWIP_6LOWPAN
//  netif_add(&(ctxt->netif), NULL, hcsim_if_init_6lowpan, tcpip_6lowpan_input);
//  lowpan6_set_pan_id(1);
//#else 
  netif_add(&(ctxt->netif), NULL, hcsim_if_init, tcpip_input);
//#endif


  (ctxt->netif).ip6_autoconfig_enabled = 1;
  netif_create_ip6_linklocal_address(&(ctxt->netif), 1);
  netif_add_ip6_address(&(ctxt->netif), ip_2_ip6(&(ctxt->ipaddr)), NULL);
  //lowpan6_set_context(1, netif_ip6_addr(&(ctxt->netif), 1));
  //lowpan6_set_context(0, netif_ip6_addr(&(ctxt->netif), 0));

  //netif_ip6_addr_set_state(&(ctxt->netif), 0,  IP6_ADDR_TENTATIVE);
  //netif_ip6_addr_set_state(&(ctxt->netif), 1,  IP6_ADDR_TENTATIVE);
  //netif_ip6_addr_set(&(ctxt->netif), 1, ip_2_ip6(&(ctxt->ipaddr)));

  //netif_add_ip6_address(&(ctxt->netif), ip_2_ip6(&(ctxt->ipaddr)), NULL);
  netif_set_default(&(ctxt->netif));
  netif_set_up(&(ctxt->netif));
  netif_ip6_addr_set_state(&(ctxt->netif), 0,  IP6_ADDR_PREFERRED);
  netif_ip6_addr_set_state(&(ctxt->netif), 1,  IP6_ADDR_PREFERRED);

  //char this_str[100];
  //ipaddr_ntoa_r(&((ctxt->netif).ip6_addr[0]), this_str, 100);
  //printf("============================= ip is %s\n", this_str);

  //netif_ip6_addr_set_state(&(ctxt->netif), 0, IP6_ADDR_TENTATIVE);   

  //Necessary steps of initializing a IPv6 interface.
  //Pre-defined variables needed for IPv6 initialization
  //static ip6_addr_t addr6; 
  /*
    IP6_ADDR2(&addr6, a, b, c, d, e, f, g, h);                      
    interface6.mtu = _mtu;//////////hcsim_if_init
    interface6.name[0] = 't';////IGNORE
    interface6.name[1] = 'p';////IGNORE
    interface6.hwaddr_len = 6;//////////low_level_init
    interface6.linkoutput = low_level_output;//////////hcsim_if_init
    interface6.ip6_autoconfig_enabled = 1;

    netif_create_ip6_linklocal_address(&interface6, 1);
    netif_add(&interface6, NULL, tapif_init, ethernet_input);
    netif_set_default(&interface6);
    netif_set_up(&interface6);      

    netif_ip6_addr_set_state(&interface6, 1, IP6_ADDR_TENTATIVE);
    _mac.copyTo(interface6.hwaddr, interface6.hwaddr_len);
    ip6_addr_copy(ip_2_ip6(interface6.ip6_addr[1]), addr6);

    interface6.output_ip6 = ethip6_output;//////////hcsim_if_init
    interface6.state = this;
    interface6.flags = NETIF_FLAG_LINK_UP | NETIF_FLAG_UP; 
  */

   
}


void read_sock(int sock, char* buffer, unsigned int bytes_length){
    size_t bytes_read = 0;
    int n;
    while (bytes_read < bytes_length){
        n = lwip_recv(sock, buffer + bytes_read, bytes_length - bytes_read, 0);
        if( n < 0 ) sock_error("ERROR reading socket");
        bytes_read += n;
        //std::cout << "Read size is " << bytes_read << std::endl;
    }

};


void write_sock(int sock, char* buffer, unsigned int bytes_length){
    size_t bytes_written = 0;
    int n;
    //std::cout << "Writing size is " << bytes_length << std::endl;
    while (bytes_written < bytes_length) {
        n = lwip_send(sock, buffer + bytes_written, bytes_length - bytes_written, 0);
        if( n < 0 ) sock_error("ERROR writing socket");
        bytes_written += n;
        //std::cout << "Written size is " << bytes_written << std::endl;
    }
};


void send_dats(void *arg)
{
  int taskID = taskManager.getTaskID(sc_core::sc_get_current_process_handle());
  OSModelCtxt* OSmodel = taskManager.get_os_ctxt( sc_core::sc_get_current_process_handle() );
  if(OSmodel->NodeID != 1){return;}
  struct netconn *conn;
  err_t err;
  lwip_context *ctxt = (lwip_context *)arg;
  /* Create a new connection identifier. */
  conn = netconn_new(NETCONN_TCP_IPV6);
  /* Bind connection to well known port number 7. */
  unsigned int buf_size;
  char* buf;
  buf_size = load_file_to_memory("./IN.JPG", &buf);

  char this_str[40];
  IP_ADDR6(&((lwip_context* )arg)->ipaddr_dest, 1, 2, 3, (4));
  ipaddr_ntoa_r(&(((lwip_context* )arg)->ipaddr_dest), this_str, 40);
  printf("Dest ip is %s\n", this_str);
  OSmodel->os_port->timeWait(10, taskID);
  err = netconn_connect(conn, &(((lwip_context* )ctxt)->ipaddr_dest), 7);

  if (err != ERR_OK) {
    printf("Error code is %d. ", err);
    std::cout << "Connection refused, "  << "error code is: "  << std::endl;
    return;
  }

  size_t *bytes_written=NULL; 
  while (1) {
    printf(" ====================== netconn_write_partly ======================\n");
    err = netconn_write_partly(conn, buf, (buf_size), NETCONN_COPY, bytes_written);
    if (err == ERR_OK) 
    {
      netconn_close(conn);
      netconn_delete(conn);
      break;
    }
  }
  printf(" ====================== netconn_write_partly done======================\n");
  free(buf);
}





void recv_dats(void *arg)
{
  OSModelCtxt* OSmodel = taskManager.get_os_ctxt( sc_core::sc_get_current_process_handle() );
  if(OSmodel->NodeID != 0){return;}

  struct netconn *conn, *newconn;
  err_t err;
  lwip_context *ctxt = (lwip_context *)arg;
  conn = netconn_new(NETCONN_TCP_IPV6);
  netconn_bind(conn, IP6_ADDR_ANY, 7);
  netconn_listen(conn);

  int taskID = taskManager.getTaskID( sc_core::sc_get_current_process_handle());
  OSmodel->Blocking_taskID = taskID;
  int push;

  while(1){
    std::cout << "====================== netconn_accept ======================"  << " time is: " << sc_core::sc_time_stamp().value() << std::endl;
    err = netconn_accept(conn, &newconn);
    if (err == ERR_OK) {
      struct netbuf *buf;
      void *data;
      u16_t len;
      int ii;
      char *img = (char*)malloc( 4000000 );
      int img_ii = 0;
      while ((err = netconn_recv(newconn, &buf)) == ERR_OK) {
        do {
             netbuf_data(buf, &data, &len);

             for(ii=0;ii<(len);ii++)
		img[img_ii+ii]=((char*)data)[ii];
             img_ii+=len;
        } while (netbuf_next(buf) >= 0);
        if((buf->p->flags) & PBUF_FLAG_PUSH){
		push++; 
		std::cout<< img_ii << "   "<< push<<std::endl;
	}
        netbuf_delete(buf);
      }
      //std::cout<< img_ii << "   "<< img[img_ii -1]<<std::endl;

      dump_mem_to_file(img_ii, "./OUT.JPG", &img);
      free(img);

      netconn_close(newconn);
      netconn_delete(newconn);

    }
  }

}

/*
udpecho_thread(void *arg)
{
  struct netconn *conn;
  struct netbuf *buf;
  char buffer[4096];
  err_t err;
  LWIP_UNUSED_ARG(arg);


  conn = netconn_new(NETCONN_UDP_IPV6);
  netconn_bind(conn, IP6_ADDR_ANY, 7);

  LWIP_ERROR("udpecho: invalid conn", (conn != NULL), return;);

  while (1) {
    err = netconn_recv(conn, &buf);
    if (err == ERR_OK) {
      //  no need netconn_connect here, since the netbuf contains the address 
      if(netbuf_copy(buf, buffer, sizeof(buffer)) != buf->p->tot_len) {
        LWIP_DEBUGF(LWIP_DBG_ON, ("netbuf_copy failed\n"));
      } else {
        buffer[buf->p->tot_len] = '\0';
        err = netconn_send(conn, buf);
        if(err != ERR_OK) {
          LWIP_DEBUGF(LWIP_DBG_ON, ("netconn_send failed: %d\n", (int)err));
        } else {
          LWIP_DEBUGF(LWIP_DBG_ON, ("got %s\n", buffer));
        }
      }
      netbuf_delete(buf);
    }
  }
}
*/

/*
void send_dats_udp(void *arg)
{
  int taskID = taskManager.getTaskID(sc_core::sc_get_current_process_handle());
  OSModelCtxt* OSmodel = taskManager.get_os_ctxt( sc_core::sc_get_current_process_handle() );
  if(OSmodel->NodeID != 1){return;}
  struct netconn *conn;
  err_t err;
  lwip_context *ctxt = (lwip_context *)arg;
  conn = netconn_new(NETCONN_UDP_IPV6);
  unsigned int buf_size;
  char* buf;
  buf_size = load_file_to_memory("./IN.JPG", &buf);

  char this_str[40];
  IP_ADDR6(&((lwip_context* )arg)->ipaddr_dest, 1, 2, 3, (4));
  ipaddr_ntoa_r(&(((lwip_context* )arg)->ipaddr_dest), this_str, 40);
  printf("Dest ip is %s\n", this_str);
  OSmodel->os_port->timeWait(10, taskID);



  size_t *bytes_written=NULL; 
  while (1) {
    printf(" ====================== netconn_write_partly ======================\n");
    err = netconn_send(conn, buf, (buf_size), NETCONN_COPY, bytes_written);
    if (err == ERR_OK) 
    {
      netconn_close(conn);
      netconn_delete(conn);
      break;
    }
  }

  printf(" ====================== netconn_write_partly done======================\n");
  free(buf);

}
*/



#endif // SC_VISION_TASK__H 
