#include <stdio.h>
#include <stdlib.h>
#include <infiniband/verbs.h>
#include <sys/socket.h>
#include <byteswap.h>
#include <netinet/in.h>

#define BUFSIZE 1024

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#endif

struct rdma_conn_data_t {
	uint32_t rkey;
	uint64_t addr;
	uint32_t qp_num;
	uint16_t lid;
} __attribute__((packed));

typedef struct rdma_conn_data_t rdma_conn_data_t;

int main(int argc, char** argv)
{
	// parameter handling
	if (argc < 2)
	{
		fprintf(stderr, "Usage: ib_test s/c [ip] [port]\n");
		return EXIT_FAILURE;
	}
	char mode = *argv[1];
	if (mode != 's' && mode != 'c')
	{
		fprintf(stderr, "Wrong mode! Use either 's' for server or 'c' for client\n");
		return EXIT_FAILURE;
	}
	if (mode == 's')
		printf("In server mode\n");
	else
	{
		if (argc < 4)
		{
			fprintf(stderr, "Please specify server IP and port\n");
			return EXIT_FAILURE;
		}
		printf("In client mode\n");
	}

	// get devices
	int num_devices;
	struct ibv_device ** devices = NULL;
	devices = ibv_get_device_list(&num_devices);
	if (!devices)
	{
		printf("ibv_get_device_list() failed.\n");
		return EXIT_FAILURE;
	}

	// open device (only 1st device is used)
	struct ibv_context * context = NULL;
	context = ibv_open_device(devices[0]);
	if (!context)
	{
		printf("ibv_open_device() failed.\n");
		return EXIT_FAILURE;
	}
	struct ibv_port_attr port_attr;
	ibv_query_port(context, 1, &port_attr);

	// create protection domain
	struct ibv_pd * pd = NULL;
	pd = ibv_alloc_pd(context);
	if (!pd)
	{
		printf("ibv_alloc_pd() failed.\n");
		return EXIT_FAILURE;
	}
	
	// register memory region
	char send_buf[BUFSIZE] = "initial content";
	if (mode == 'c')
	{
		printf("Content of buffer on client: %s\n", send_buf);
	}
	struct ibv_mr * mr;
	mr = ibv_reg_mr(pd, send_buf, BUFSIZE, IBV_ACCESS_REMOTE_WRITE |
		IBV_ACCESS_LOCAL_WRITE);
	if (!mr)
	{
		printf("ibv_reg_mr() failed.\n");
		return EXIT_FAILURE;
	}

	// create completion queue
	struct ibv_cq * cq;
	cq = ibv_create_cq(context, 10, NULL, NULL, 0);
	if (!cq)
	{
		printf("ibv_create_cq() failed.\n");
		return EXIT_FAILURE;
	}

	// create queue pair
	struct ibv_qp * qp;
	struct ibv_qp_init_attr init_attr = {
		.send_cq = cq,
		.recv_cq = cq,
		.cap = {
			.max_send_wr = 1,
			.max_recv_wr = 10,
			.max_send_sge = 1,
			.max_recv_sge = 1
		},
		.qp_type = IBV_QPT_RC//,
		//.sq_sig_all = 1
	};
	qp = ibv_create_qp(pd, &init_attr);
	if (!qp)
	{
		printf("ibv_create_qp() failed.\n");
		return EXIT_FAILURE;
	}

	struct ibv_qp_attr attr;
	// change qp to INIT state
	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.pkey_index = 0;
	attr.port_num = 1;
	attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
	if (ibv_modify_qp(qp, &attr,
		IBV_QP_STATE |
		IBV_QP_PKEY_INDEX |
		IBV_QP_PORT |
		IBV_QP_ACCESS_FLAGS))
	{
		printf("ibv_modify_qp() failed.\n");
		return EXIT_FAILURE;
	}

	// exchange connection information
	struct sockaddr_in address;
	int sd, conn;
	rdma_conn_data_t local_conn_data, remote_conn_data, tmp_conn_data;

	local_conn_data.rkey = htonl(mr->rkey);
	local_conn_data.addr = htonll((uintptr_t)mr->addr);
	local_conn_data.qp_num = htonl(qp->qp_num);
	local_conn_data.lid = htons(port_attr.lid);
	if (mode == 's')
	{
		int addrlen = sizeof(address);
		sd = socket(AF_INET, SOCK_STREAM, 0);
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(8999);
		bind(sd, (struct sockaddr *)&address, sizeof(address));
		listen(sd, 1);
		printf("Waiting for client to connect at 0.0.0.0:8999\n");
		conn = accept(sd, NULL, 0);
		read(conn, (char *)&tmp_conn_data, sizeof(rdma_conn_data_t));
		write(conn, (char *)&local_conn_data, sizeof(rdma_conn_data_t));
	}
	else
	{
		// prepare the buffer with rkey & memory addr
		sd = socket(AF_INET, SOCK_STREAM, 0);
		address.sin_family = AF_INET;
		address.sin_port = htons(atoi(argv[3]));
		inet_pton(AF_INET, argv[2], &address.sin_addr);
		connect(sd, (struct sockaddr *)&address, sizeof(address));
		write(sd, (char *)&local_conn_data, sizeof(rdma_conn_data_t));
		read(sd, (char *)&tmp_conn_data, sizeof(rdma_conn_data_t));
	}
	remote_conn_data.addr = ntohll(tmp_conn_data.addr);
	remote_conn_data.rkey = ntohl(tmp_conn_data.rkey);
	remote_conn_data.qp_num = ntohl(tmp_conn_data.qp_num);
	remote_conn_data.lid = ntohs(tmp_conn_data.lid);

	// change qp to RTR state
	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = IBV_MTU_256;
	attr.dest_qp_num = remote_conn_data.qp_num;
	attr.rq_psn = 0;
	attr.max_dest_rd_atomic = 1;
	attr.min_rnr_timer = 0x12;
	attr.ah_attr.is_global = 0;
	attr.ah_attr.dlid = remote_conn_data.lid;
	attr.ah_attr.sl = 0;
	attr.ah_attr.src_path_bits = 0;
	attr.ah_attr.port_num = 1;
	if (ibv_modify_qp(qp, &attr,
		IBV_QP_STATE |
		IBV_QP_AV |
		IBV_QP_PATH_MTU |
		IBV_QP_DEST_QPN |
		IBV_QP_RQ_PSN |
		IBV_QP_MAX_DEST_RD_ATOMIC |
		IBV_QP_MIN_RNR_TIMER))
	{
		printf("ibv_modify_qp() failed.\n");
		return EXIT_FAILURE;
	}

	// send and recv
	struct ibv_sge sg_list = {
		.addr = (uint64_t)send_buf,
		.length = BUFSIZE,
		.lkey = mr->lkey
	};
	if (mode == 's')
	{
		// change qp to RTS state
		memset(&attr, 0, sizeof(attr));
		attr.qp_state = IBV_QPS_RTS;
		attr.sq_psn = 0;
		attr.timeout = 0x12;
		attr.retry_cnt = 6;
		attr.rnr_retry = 0;
		attr.max_rd_atomic = 1;
		if (ibv_modify_qp(qp, &attr,
			IBV_QP_STATE |
			IBV_QP_TIMEOUT |
			IBV_QP_RETRY_CNT |
			IBV_QP_RNR_RETRY |
			IBV_QP_SQ_PSN |
			IBV_QP_MAX_QP_RD_ATOMIC))
		{
			printf("ibv_modify_qp() failed.\n");
			return EXIT_FAILURE;
		}
		// post send
		struct ibv_send_wr send_wr = {
			.next = NULL,
			.sg_list = &sg_list,
			.num_sge = 1,
			.opcode = IBV_WR_RDMA_WRITE,
			.wr.rdma.remote_addr = remote_conn_data.addr,
			.wr.rdma.rkey = remote_conn_data.rkey,
			.send_flags = IBV_SEND_SIGNALED
		};
		struct ibv_send_wr * bad_wr;
		char * message = "hello world!";
		strcpy(send_buf, message);
		if (ibv_post_send(qp, &send_wr, &bad_wr))
		{
			printf("ibv_post_send() failed.\n");
			return EXIT_FAILURE;
		}
		printf("Sent message: %s\n", message);
	}
	//else
	//{
	//	struct ibv_recv_wr recv_wr = {
	//		.next = NULL,
	//		.wr_id = 0,
	//		.sg_list = &sg_list,
	//		.num_sge = 1
	//	};
	//	struct ibv_recv_wr * bad_wr;
	//	if (ibv_post_recv(qp, &recv_wr, &bad_wr))
	//	{
	//		printf("ibv_post_recv() failed.\n");
	//		return EXIT_FAILURE;
	//	}
	//}

	// poll for completion on both sides
	if (mode == 's')
	{
		struct ibv_wc wc;
		int poll_result;
		do {
			poll_result = ibv_poll_cq(cq, 1, &wc);
		} while (poll_result == 0);
		if (poll_result < 0)
			fprintf(stderr, "ibv_poll_cq() failed\n");

		if (wc.status != IBV_WC_SUCCESS)
		{
			fprintf(stderr, "Work completion failed with %s\n", ibv_wc_status_str(wc.status));
		}
	}
	// synchronize here
	if (mode == 's')
	{
		read(conn, (char *)&tmp_conn_data, sizeof(rdma_conn_data_t));
		write(conn, (char *)&local_conn_data, sizeof(rdma_conn_data_t));
	}
	else
	{
		write(sd, (char *)&local_conn_data, sizeof(rdma_conn_data_t));
		read(sd, (char *)&tmp_conn_data, sizeof(rdma_conn_data_t));
	}
	printf("Sync\n");
	if (mode == 'c')
	{
		printf("Content of buffer on client: %s\n", send_buf);
	}
	
	// finalize everything
	close(sd);
	if (mode == 's')
		close(conn);
	ibv_destroy_qp(qp);
	ibv_destroy_cq(cq);
	ibv_dereg_mr(mr);
	ibv_dealloc_pd(pd);
	ibv_close_device(context);
	ibv_free_device_list(devices);
}
