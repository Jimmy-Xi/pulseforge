PYTHON ?= python3

.PHONY: test benchmark userspace kernel clean

test:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -v

benchmark:
	$(PYTHON) tools/benchmark.py

userspace:
	$(MAKE) -C userspace

kernel:
	$(MAKE) -C kernel

clean:
	$(MAKE) -C userspace clean
	-$(MAKE) -C kernel clean

