const User = require('../models/User');
const bcrypt = require('bcryptjs');

class UserService {
  async createUser(userData) {
    const hashedPassword = await bcrypt.hash(userData.password, 10);
    
    const user = new User({
      ...userData,
      password: hashedPassword
    });
    
    await user.save();
    return this.sanitizeUser(user);
  }

  async findByEmail(email) {
    return await User.findOne({ email });
  }

  async findByUsername(username) {
    return await User.findOne({ username });
  }

  async findById(id) {
    const user = await User.findById(id);
    return user ? this.sanitizeUser(user) : null;
  }

  async findByIds(ids) {
    const users = await User.find({ _id: { $in: ids } });
    return users.map(user => this.sanitizeUser(user));
  }

  async updateUser(id, updateData) {
    if (updateData.password) {
      updateData.password = await bcrypt.hash(updateData.password, 10);
    }
    
    const user = await User.findByIdAndUpdate(
      id,
      { $set: updateData },
      { new: true, runValidators: true }
    );
    
    return user ? this.sanitizeUser(user) : null;
  }

  async updateLoginInfo(id, ip) {
    await User.findByIdAndUpdate(id, {
      $set: {
        lastLoginAt: new Date(),
        loginIP: ip
      }
    });
  }

  async deleteUser(id) {
    return await User.findByIdAndDelete(id);
  }

  async listUsers(filter = {}, options = {}) {
    const { page = 1, limit = 20, sort = { createdAt: -1 } } = options;
    const skip = (page - 1) * limit;
    
    const [users, total] = await Promise.all([
      User.find(filter)
        .sort(sort)
        .skip(skip)
        .limit(limit)
        .select('-password'),
      User.countDocuments(filter)
    ]);
    
    return {
      users,
      total,
      page,
      limit,
      totalPages: Math.ceil(total / limit)
    };
  }

  async comparePassword(plainPassword, hashedPassword) {
    return await bcrypt.compare(plainPassword, hashedPassword);
  }

  sanitizeUser(user) {
    if (!user) return null;
    const userObject = user.toObject ? user.toObject() : user;
    delete userObject.password;
    return userObject;
  }
}

module.exports = new UserService();
