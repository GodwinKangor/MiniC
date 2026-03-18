extern void print(int);
extern int read();

int func(int n){
	int max = 0;
	int i;
	int a;
	i = 0;
	
	while (i < n){ 
		a = read();
		if (a > max)
			max = a;
		i = i + 1;
	}
	
	return max;
}
