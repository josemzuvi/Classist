#include "classist.h"

package(animal);

class(Animal)
	var(char*, action);
	func(void, act, ());
	static(Animal*, new, (char* action));
endClass
