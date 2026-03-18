extern void print(int);
extern int read();

int func(int n){
	int res;
	res = n;
	
	while (res > 1){
	 	res = res - 2;
	}

	return res;
}
