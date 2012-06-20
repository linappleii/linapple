#ifndef GENERIC_LIST
#define GENERIC_LIST

/*
Funciones para LISTAS:

	void Delete()
	void Instance(List<T> &l)
	void Rewind(void)
	void Forward(void)
	void Next(void)
	void Prev(void)
	T *GetObj(void)
	LLink<T> *GetPos(void)
	bool EmptyP()
	bool EndP()
	bool LastP()
	bool BeginP()
	void Insert(T *o)
	void Add(T *o)
	void AddAfter(LLink<T> *pos,T *o)
	bool Iterate(T *&o)
	T *ExtractIni(void)
	T *Extract(void)
	bool MemberP(T *o)
	T *MemberGet(T *o)
	bool MemberRefP(T *o)
	int Length()
	void Copy(List l)
	bool DeleteElement(T *o)
	T *GetRandom(void)
	void SetNoOriginal(void)
	void SetOriginal(void)


	bool operator==(List<T> &l)
	T *operator[](int index)
*/

template <class T> class LLink {
public:
	LLink<T>(T *o,LLink<T> *n=NULL) {
		obj=o;next=n;
	};
	~LLink<T>() {delete obj;
				 if (next!=NULL) delete next;};
	inline LLink<T> *Getnext() {return next;};
	inline void Setnext(LLink<T> *n) {next=n;};
	inline T *GetObj() {return obj;};
	inline void Setobj(T *o) {obj=o;};

	void Anade(T *o) {
		if (next==NULL) {
			LLink<T> *node=new LLink<T>(o);
			next=node;
		} else {
			next->Anade(o);
		}
		};

private:
	T *obj;
	LLink<T> *next;
};

template <class T> class List {
public:
	List<T>() {list=NULL;act=NULL;top=NULL;original=true;};
	~List<T>() {
		if (original) {
			delete list;
		} /* if */
	};
	List<T>(List<T> &l) {list=l.list;act=list;top=l.top;original=false;};

	void Delete() {
		if (original) {
			delete list;
		} /* if */
		list=NULL;
		act=NULL;
		top=NULL;
	};

	void Instance(List<T> &l) {list=l.list;act=list;top=l.top;original=false;};
	void Rewind(void) {act=list;};
	void Forward(void) {act=top;};
	void Next(void) {
		if (act!=NULL) act=act->Getnext();
	};

	void Prev(void) {
		LLink<T> *tmp;

		if (act!=list) {
			tmp=list;
			while(tmp->Getnext()!=act) tmp=tmp->Getnext();
			act=tmp;
		} /* if */
	};

	T *GetObj(void) {return act->GetObj();};
	LLink<T> *GetPos(void) {return act;};
	bool EmptyP() {return list==NULL;};
	bool EndP() {return act==NULL;};
	bool LastP() {return act==top;};
	bool BeginP() {return act==list;};

	void Insert(T *o) {
		if (list==NULL) {
			list=new LLink<T>(o);
			top=list;
		} else {
			list=new LLink<T>(o,list);
		} /* if */
	};

	void Add(T *o) {
		if (list==NULL) {
			list=new LLink<T>(o);
			top=list;
		} else {
			top->Anade(o);
			top=top->Getnext();
		} /* if */
	};

	void AddAfter(LLink<T> *pos,T *o)
	{
		if (pos==NULL) {
			if (list==NULL) {
				list=new LLink<T>(o);
				top=list;
			} else {
				list=new LLink<T>(o,list);
			} /* if */
		} else {
			LLink<T> *nl=new LLink<T>(o);

			nl->Setnext(pos->Getnext());
			pos->Setnext(nl);
			if (nl->Getnext()==NULL) top=nl;
		} /* if */
	} /* AddAfter */

	T *operator[](int index) {
		LLink<T> *tmp=list;
		while(tmp!=NULL && index>0) {
			tmp=tmp->Getnext();
			index--;
		} /* while */
		if (tmp==NULL) throw;
		return tmp->GetObj();
	};

// this functions was added by me, beom beotiger, for sorting these Lists! Ha-ha-ha ^_^
	void Swap(int index1, int index2) {	// swap two object with index1, index2
		LLink<T> *tmp=list;
		LLink<T> *tmp2=list;
		T *o;
		while(tmp!=NULL && index1 > 0) {	// find List obj for index1
			tmp=tmp->Getnext();
			index1--;
		} /* while */
		if (tmp==NULL) throw;
		while(tmp2!=NULL && index2 > 0) {	// find List object for index2
			tmp2=tmp2->Getnext();
			index2--;
		} /* while */
		if (tmp2==NULL) throw;

		o = tmp->GetObj();		// temp
		tmp->Setobj(tmp2->GetObj());
		tmp2->Setobj(o);
//		return tmp->GetObj(); inline T *GetObj() {return obj;};
	};/* Swap */

	bool Iterate(T *&o) {
		if (EndP()) return false;
		o=act->GetObj();
		act=act->Getnext();
		return true;
	} /* Iterate */

	T *ExtractIni(void) {
		LLink<T> *tmp;
		T *o;

		if (list==NULL) return NULL;
		o=list->GetObj();
		tmp=list;
		list=list->Getnext();
		tmp->Setnext(NULL);
		if (act==tmp) act=list;
		if (top==act) top=NULL;
		tmp->Setobj(NULL);
		delete tmp;
		return o;
	} /* ExtractIni */

	T *Extract(void) {
		LLink<T> *tmp,*tmp2=NULL;
		T *o;

		if (list==NULL) return NULL;
		tmp=list;
		while(tmp->Getnext()!=NULL) {
			tmp2=tmp;
			tmp=tmp->Getnext();
		} /* while */
		o=tmp->GetObj();
		if (tmp2==NULL) {
			list=NULL;
			top=NULL;
			act=NULL;
		} else {
			tmp2->Setnext(NULL);
			top=tmp2;
		} /* if */

		if (act==tmp) act=top;
		tmp->Setobj(NULL);
		delete tmp;
		return o;
	} /* Extract */

	bool MemberP(T *o) {
		LLink<T> *tmp;
		tmp=list;
		while(tmp!=NULL) {
			if (*(tmp->GetObj())==*o) return true;
			tmp=tmp->Getnext();
		} /* while */
		return false;
	} /* MemberP */

	T *MemberGet(T *o) {
		LLink<T> *tmp;
		tmp=list;
		while(tmp!=NULL) {
			if (*(tmp->GetObj())==*o) return tmp->GetObj();
			tmp=tmp->Getnext();
		} /* while */
		return NULL;
	} /* MemberGet */

	bool MemberRefP(T *o) {
		LLink<T> *tmp;
		tmp=list;
		while(tmp!=NULL) {
			if (tmp->GetObj()==o) return true;
			tmp=tmp->Getnext();
		} /* while */
		return false;
	} /* MemberRefP */

	int Length() {
		LLink<T> *tmp;
		int count=0;

		tmp=list;
		while(tmp!=NULL) {
			tmp=tmp->Getnext();
			count++;
		} /* while */
		return count;
	};

	void Copy(List l) {
		T *o;
		Delete();
		original=true;
		l.Rewind();
		while(l.Iterate(o)) {
			o=new T(*o);
			Add(o);
		} /* while */
	} /* Copy */

	bool DeleteElement(T *o)
	{
		LLink<T> *tmp1,*tmp2;

		tmp1=list;
		tmp2=NULL;
		while(tmp1!=NULL && tmp1->GetObj()!=o) {
			tmp2=tmp1;
			tmp1=tmp1->Getnext();
		} /* while */

		if (tmp1!=NULL) {
			if (tmp2==NULL) {
				/* Eliminar el primer elemento de la lista: */
				list=list->Getnext();
				tmp1->Setnext(NULL);
				if (act==tmp1) act=list;
				tmp1->Setobj(NULL);
				delete tmp1;
			} else {
				/* Eliminar un elemento intermedio: */
				tmp2->Setnext(tmp1->Getnext());
				if (act==tmp1) act=tmp1->Getnext();
				if (top==tmp1) top=tmp2;
				tmp1->Setnext(NULL);
				tmp1->Setobj(NULL);
				delete tmp1;
			} /* if */
			return true;
		} else {
			return false;
		} /* if */

	} /* DeleteOne */

	T *GetRandom(void) {
		int i,l=Length();
		i=((rand()*l)/RAND_MAX);
		if (i==l) i=l-1;

		return operator[](i);
	} /* GetRandom */

	bool operator==(List<T> &l) {
		LLink<T> *tmp1,*tmp2;

		tmp1=list;
		tmp2=l.list;
		while(tmp1!=NULL && tmp2!=NULL) {
			if (!((*(tmp1->GetObj()))==(*(tmp2->GetObj())))) return false;
			tmp1=tmp1->Getnext();
			tmp2=tmp2->Getnext();
		} /* while */
		return tmp1==tmp2;
	} /* == */


	void SetNoOriginal(void) {original=false;}
	void SetOriginal(void) {original=true;}

private:
	bool original;
	LLink<T> *list,*top;
	LLink<T> *act;
};


#endif
