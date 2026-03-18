extern void print(int);
extern int read();

int func(int n){
	int a1;
	int a2;
	int c;
	int t;
	
	a1 = 1;
	a2 = 1;
	c = 0;

	while (c < n){ 
		print(a1);
		c = c + 1;
		t = a2;
		a2 = a1 + a2;
		a1 = t;
	}
	
	return 1;
}
