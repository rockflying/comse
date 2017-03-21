## What is Comse?

Comse is a lightweigt common search engine,and it support fulltext search(index cross search and index sum search).

Main technology:

* dynamically increseaing or shrinking index data structures(for fast insert,delete index)
* use linked list to save index,and every linked node has one adaptive size linear table(for fast search index)
* use json data via http post to Interact(easy to use curl、python、php etc...)


## Building Comse

If First build:

```
git clone https://github.com/dodng/comse.git
cd comse/core
sh -x build.sh
```

else:

```
cd comse/core
make clean;make
```

## Running Comse
Run these in different consoles:

```
cd comse/service
sh -x run.sh
```
