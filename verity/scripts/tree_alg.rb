#!/usr/bin/ruby
# Rapid prototyping helper for the math impaired.
# Copyright 2010 The Chromium OS Authors. All rights reserved.
# Licensed under a BSD-style license found at chromium.googlesource.com.

class Fixnum
def ffs()
  return 0 if self.zero?
  s = self.to_s(2)
  bit = s.index("1")
  return (s.length) - bit
end
def align
end
end

class BHTree
  attr_accessor :depth
  attr_accessor :max_children
  attr_accessor :max_children_shift
  attr_accessor :hash_len
  attr_accessor :offset
  attr_accessor :leaf_count
  attr_accessor :end_offset
  def initialize(depth, max_children, leaf_count)
    self.depth = depth
    self.max_children = max_children
    self.max_children_shift = max_children.ffs() - 1
    self.hash_len = 256 / 8
    self.offset = 0 # in bytes
    self.leaf_count = leaf_count
    self.end_offset = 0
  end
  
  def allocate()
    sector = 0
    depth = 0
    last_index = self.leaf_count - 1
    print "last_index: #{last_index}"
    depth.upto(self.depth - 1) { |depth|
      print "\t-----\n"
      print "\tdepth: #{depth}\n"
      child_count = (last_index >> ( ((self.depth - depth) * self.max_children_shift))) #% self.max_children
      child_count += 1
      print "\tallocating #{child_count} entries\n"
      sector_inc = (1 << ((self.depth - depth) * self.max_children_shift))
      sector_inc = self.leaf_count if sector_inc > self.leaf_count
      sector_inc *= self.hash_len
      sector += sector_inc
      print "\tadding #{sector_inc/512} sectors\n"
    }
    sector /= 512
    sector += self.offset
    self.end_offset = sector
    print "end sector: #{sector}\n\n"
  end

  def walk(leaf_index)
    depth = self.depth
    print "leaf_index: #{leaf_index}"
    sector = self.end_offset
    print "leaf_index too big\n" if (leaf_index > self.leaf_count)
    self.depth.downto(1) { |depth|
      print "\t-----\n"
      print "\tdepth: #{depth}\n"
      print "\texpected below: #{1 << (depth * self.max_children_shift)}\n"
      child_index = (leaf_index >> ( (depth * self.max_children_shift))) % self.max_children
      print "\tchild_index: #{child_index}\n"
      print "\tsector: #{sector}\n"
      sector -= ((1 << (((depth-1) * self.max_children_shift))) * self.hash_len) /512
    }
  end
end

tree = BHTree.new(3, 128, 163840)
tree.walk(1)
tree.walk(127)
tree.walk(128)
tree.walk(255)
tree.walk(256)
tree.walk(500)
tree.walk(512)
tree.walk(16383)
tree.walk(16384)
tree.walk(16385)
tree.walk(32767)
tree.walk(32768)
tree.walk(163840)
tree.walk(170000)
tree.walk(327688)

print "ALLOCATE\n"
tree = BHTree.new(2, 128, 16384)
tree.allocate

tree = BHTree.new(2, 128, 1024)
tree.allocate
tree.walk(1000)

tree = BHTree.new(3, 128, 1024)
tree.allocate
