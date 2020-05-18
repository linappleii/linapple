// Motorola MC6821 PIA

typedef void (*mem_write_handler)(void *objFrom, void *objTo, int nAddr, unsigned char byData);

typedef struct _STWriteHandler {
  void *objTo;
  mem_write_handler func;
} STWriteHandler;

#define  PIA_DDRA  0
#define  PIA_CTLA  1
#define  PIA_DDRB  2
#define  PIA_CTLB  3

class C6821 {
public:
  C6821();

  virtual ~C6821();

  unsigned char GetPB();

  unsigned char GetPA();

  void SetPB(unsigned char byData);

  void SetPA(unsigned char byData);

  void SetCA1(unsigned char byData);

  void SetCA2(unsigned char byData);

  void SetCB1(unsigned char byData);

  void SetCB2(unsigned char byData);

  void Reset();

  unsigned char Read(unsigned char byRS);

  void Write(unsigned char byRS, unsigned char byData);

  void UpdateInterrupts();

  void SetListenerA(void *objTo, mem_write_handler func);

  void SetListenerB(void *objTo, mem_write_handler func);

  void SetListenerCA2(void *objTo, mem_write_handler func);

  void SetListenerCB2(void *objTo, mem_write_handler func);

protected:
  unsigned char m_byIA;
  unsigned char m_byCA1;
  unsigned char m_byICA2;
  unsigned char m_byOA;
  unsigned char m_byOCA2;
  unsigned char m_byDDRA;
  unsigned char m_byCTLA;
  unsigned char m_byIRQAState;

  unsigned char m_byIB;
  unsigned char m_byCB1;
  unsigned char m_byICB2;
  unsigned char m_byOB;
  unsigned char m_byOCB2;
  unsigned char m_byDDRB;
  unsigned char m_byCTLB;
  unsigned char m_byIRQBState;

  STWriteHandler m_stOutA;
  STWriteHandler m_stOutB;
  STWriteHandler m_stOutCA2;
  STWriteHandler m_stOutCB2;
  STWriteHandler m_stOutIRQA;
  STWriteHandler m_stOutIRQB;
};
