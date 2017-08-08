/*
 * Generated by dtrace(1M).
 */

#ifndef	_FEDERATED_PROBES_H
#define	_FEDERATED_PROBES_H



#ifdef	__cplusplus
extern "C" {
#endif

#if _DTRACE_VERSION

#define	FEDERATED_CLOSE() \
	__dtrace_federated___close()
#define	FEDERATED_CLOSE_ENABLED() \
	__dtraceenabled_federated___close()
#define	FEDERATED_OPEN() \
	__dtrace_federated___open()
#define	FEDERATED_OPEN_ENABLED() \
	__dtraceenabled_federated___open()


extern void __dtrace_federated___close(void);
extern int __dtraceenabled_federated___close(void);
extern void __dtrace_federated___open(void);
extern int __dtraceenabled_federated___open(void);

#else

#define	FEDERATED_CLOSE()
#define	FEDERATED_CLOSE_ENABLED() (0)
#define	FEDERATED_OPEN()
#define	FEDERATED_OPEN_ENABLED() (0)

#endif


#ifdef	__cplusplus
}
#endif

#endif	/* _FEDERATED_PROBES_H */
