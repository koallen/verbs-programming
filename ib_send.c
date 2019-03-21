#include <stdio.h>
#include <stdlib.h>
#include <infiniband/verbs.h>

#define BUFSIZE 1024

int main()
{
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

	// create protection domain
	struct ibv_pd * pd = NULL;
	pd = ibv_alloc_pd(context);
	if (!pd)
	{
		printf("ibv_alloc_pd() failed.\n");
		return EXIT_FAILURE;
	}
	
	// register memory region
	char send_buf[BUFSIZE];
	struct ibv_mr * mr;
	mr = ibv_reg_mr(pd, send_buf, BUFSIZE, 0);
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
		.qp_type = IBV_QPT_RC
	};
	qp = ibv_create_qp(pd, &init_attr);
	if (!qp)
	{
		printf("ibv_create_qp() failed.\n");
		return EXIT_FAILURE;
	}

	// change qp to INIT state
	struct ibv_qp_attr attr1 = {
		.qp_state = IBV_QPS_INIT,
		.pkey_index = 0,
		.port_num = 1,
		.qp_access_flags = 0
	};
	ibv_modify_qp(qp, &attr1, IBV_QP_STATE |
		IBV_QP_PKEY_INDEX |
		IBV_QP_PORT |
		IBV_QP_ACCESS_FLAGS);

	// exchange connection information

	// change qp to RTR state
	// change qp to RTS state

	// post send
	struct ibv_sge sg_list = {
		.addr = (uint64_t)send_buf,
		.length = BUFSIZE,
		.lkey = mr->lkey
	};
	struct ibv_send_wr send_wr = {
		.next = NULL,
		.sg_list = &sg_list,
		.num_sge = 1,
		.opcode = IBV_WR_SEND
	};
	struct ibv_send_wr * bad_wr;
	if (ibv_post_send(qp, &send_wr, &bad_wr))
	{
		printf("ibv_post_send() failed.\n");
		return EXIT_FAILURE;
	}
	
	// finalize everything
	ibv_destroy_qp(qp);
	ibv_destroy_cq(cq);
	ibv_dereg_mr(mr);
	ibv_dealloc_pd(pd);
	ibv_close_device(context);
	ibv_free_device_list(devices);
}
