extern void print(int);
extern int read();

int func(int n){
	int prod;
	int i;
	i = 1;
	prod = 1;;
	
	while (i < n){ 
		i = i + 1;
		prod = prod * i;
	}
	
	return prod;
}
